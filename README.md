# Micro-kernel-using-C- (AArch64 on QEMU)
- Minimal bare-metal AArch64 kernel targeting QEMU `-machine virt`
- Toolchain via Docker (LLVM + LLD + QEMU)
- MMU + I/D caches enabled early (to surface DMA coherency issues)

## Quick start (QEMU virt)

```sh
./scripts/devshell.sh
make run
```

Run the CI-equivalent smoke test locally:

```sh
./scripts/smoke_run.sh
```

## Raspberry Pi 4 (bring-up)

- Build image: `make rpi4`
- Outputs: `build/kernel8.img` (firmware) and `build/kernel_rpi4.elf` (debug)
- Instructions: `docs/rpi4.md`

## Build knobs

All knobs are Make variables:

- `PLATFORM=virt|rpi4` (default: `virt`)
- `DMA_WINDOW_POLICY=CACHEABLE|NONCACHEABLE` (default: `CACHEABLE`)
- `DMA_LAB_MODE=0|1|2|...` (default: `0`)
- `SCHED_POLICY=RR|PRIO` (default: `RR`)
- `MUTEX_PI=0|1` (default: `1`)
- `SYNC_LAB_MODE=0|1|...` (default: `0`)
- `MEM_LAB_MODE=0|1` (default: `0`)
- `RPI4_UART_CLOCK_HZ=<hz>` (only used when building `PLATFORM=rpi4`)

## Memory layout
The linker script at `boot/kernel.ld` exposes a handful of global symbols that
describe the static memory map:

- `__boot_stack_top` marks the end of the 16 KB stack used strictly during the
  boot sequence. On AArch64, the stack pointer must remain 16-byte aligned, so
  the linker keeps this top-of-stack naturally aligned.
- `__irq_stack_cpu0` and its aliases `__irq_stack_cpu0_end` /
  `__irq_stack_cpu0_top` reserve another 16 KB stack for CPU 0 interrupt
  handlers. Additional CPUs can follow the same naming pattern as they come
  online.
- `_heap_start`..`_heap_end` carve out a contiguous 1 MB kernel heap used for
  simple bump-style allocations before a full allocator exists. The region is
  aligned to 4 KiB boundaries so later MMU attributes can be applied without
  splitting pages.
- `__dma_nc_start`..`__dma_nc_end` describe a 128 KB window set aside for
  DMA experiments. The symbol name is historical: the mapping policy for this
  window is configurable (cacheable vs non-cacheable) so coherency bugs can be
  reproduced and fixed properly. The 4 KiB alignment matches page granularity,
  simplifying cache maintenance and memory attribute updates.

Additional memory allocator + fragmentation notes: `docs/memory.md`.

## MMU, caches, and DMA coherency

The kernel enables the EL1 MMU and I/D caches early in `src/kmain.cc`, and the
boot log prints the resulting `SCTLR_EL1` bits (expect `C=1, I=1, M=1`). This is
required for the cache-maintenance-based non-coherent DMA model used by the DMA
self-test.

### DMA window policy

Build-time knobs (see `Makefile`):

- `DMA_WINDOW_POLICY=CACHEABLE` (default): `__dma_nc_start`..`__dma_nc_end` are
  mapped as Normal WBWA (cacheable). DMA requires explicit cache maintenance;
  this is the default to surface coherency bugs.
- `DMA_WINDOW_POLICY=NONCACHEABLE`: the same window is mapped as Normal
  non-cacheable (control group).

The boot log prints the active policy as `[dma-policy] CACHEABLE|NONCACHEABLE`.

### Non-cacheable alias mapping

RAM is also mapped through a non-cacheable alias (VA offset `+0x40000000`, i.e.
`0x80000000..0xBFFFFFFF` aliases `0x40000000..0x7FFFFFFF`). For buffers inside
the DMA window, `include/dma.h` provides strict helpers:

- `dma_window_to_nocache_alias(p, len)`
- `dma_window_from_nocache_alias(p, len)`

### DMA lab mode

`DMA_LAB_MODE!=0` switches `kmain()` into a deterministic DMA coherency lab. It
runs a suite of reproducible fail→fix cases (stale reads, descriptor publish
ordering, completion flag polling, cache-line sharing hazards) and then halts
instead of starting the scheduler/IRQs.

Examples:

- Run the full suite: `DMA_LAB_MODE=1 DMA_WINDOW_POLICY=CACHEABLE scripts/dma_lab_run.sh`
- Run a single case (best-effort): `DMA_LAB_MODE=3 DMA_WINDOW_POLICY=CACHEABLE scripts/dma_lab_run.sh`

## Design case studies (STAR)

This project intentionally enables caches early and treats DMA as non-coherent,
because most real firmware/driver bugs only show up once the CPU is caching.

### 1) Enable MMU + caches without breaking MMIO

- Situation: the kernel needs caches enabled to expose real DMA coherency bugs,
  but UART/GIC/timer are MMIO and must not be cached.
- Task: turn on `SCTLR_EL1.M/C/I` while keeping MMIO strongly ordered and keeping
  the existing IRQ/scheduler flow stable.
- Action:
  - Build a minimal 4 KiB-granule identity map (`src/arch/aarch64/mmu.cc` for
    QEMU virt; `src/arch/aarch64/mmu_rpi4.cc` for RPi4).
  - Map MMIO windows as `Device-nGnRE` and kernel/RAM as `Normal WBWA`, plus a
    `Normal non-cacheable` alias for lab/device-view experiments.
  - Program `MAIR_EL1/TCR_EL1/TTBR0_EL1` and enable `SCTLR_EL1` while preserving
    architectural RES1 bits (no hard-coded constants).
- Result:
  - Boot logs include `[mmu] enabled ... (C=1, I=1, M=1)` and the system keeps
    printing via UART with interrupts and scheduling still working on QEMU.

### 2) Linux-style DMA sync semantics (non-coherent model)

- Situation: the DMA engine is a software memcpy, but we intentionally model it
  as a non-coherent device so cache bugs are reproducible on QEMU.
- Task: make the DMA self-test pass reliably with caches enabled without
  forcing all buffers to be non-cacheable.
- Action:
  - Implement explicit coherency primitives in `src/dma.cc`:
    - `dma_prepare_to_device(buf,len)`: clean to PoC + `dma_wmb()` (`dmb oshst`)
    - `dma_complete_from_device(buf,len)`: `dma_rmb()` (`dmb oshld`) + invalidate
  - Ensure descriptor writes are cleaned and ordered before publishing to the
    “device”.
  - Guard cache maintenance so we do not issue DC operations on non-cacheable or
    Device mappings.
- Result:
  - The default QEMU virt path prints `[DMA OK]` under `SCTLR_EL1.C=1`.

### 3) Deterministic DMA coherency lab (fail → fix evidence)

- Situation: DMA coherency issues can be timing-sensitive and hard to reproduce.
- Task: build a lab-grade harness that can demonstrate a bug, apply the correct
  fix, and produce evidence — all in a single run.
- Action:
  - Provide a non-cacheable alias mapping and strict VA conversion helpers
    (`include/dma.h`).
  - Add `DMA_LAB_MODE` test cases (`src/dma_lab.cc`) that exercise common
    non-coherent failure modes (stale reads, publish ordering, completion flag
    races, cache-line sharing hazards) and then apply the corresponding fixes.
- Result:
  - `scripts/dma_lab_run.sh` can reproduce and validate coherency fixes without
    needing real DMA hardware.

### 4) CI smoke tests that fail on “silent badness”

- Situation: regressions (e.g. caches accidentally off, wrong barriers, or QEMU
  unimplemented behavior) can be masked by unrelated log output.
- Task: ensure CI fails fast on silent correctness regressions.
- Action (`scripts/smoke_run.sh`):
  - Assert `[mmu] enabled ... (C=1, I=1, M=1)` is present.
  - Compile `dma_*` barriers to assembly and grep for `dmb osh*` to lock in
    semantics.
  - Parse QEMU trace output for `guest_errors/unimp` and fail immediately.
- Result:
  - CI catches common “looks fine but is wrong” failures early and deterministically.

### 5) Platform abstraction for real boards (RPi4 bring-up scaffold)

- Situation: QEMU virt is a great testbed, but real boards differ in boot flow,
  UART base/clock/GPIO muxing, and interrupt controller details.
- Task: add an extendable platform layer without breaking the existing QEMU virt
  build/run/CI pipeline.
- Action:
  - Add `platform_early_init()` and platform selection (`PLATFORM=...`) to keep
    board-specific constants out of core code (`include/platform.h`,
    `src/platform/*`).
  - Add an RPi4 boot stub (`boot/rpi4_start.S`) with EL2→EL1 transition markers,
    plus an RPi4 linker script (`boot/kernel_rpi4.ld`) and `make rpi4` image
    output (`kernel8.img`).
- Result:
  - You can build a firmware-loadable `kernel8.img` while keeping QEMU virt CI
    unchanged.

## Host build prerequisites

The `Makefile` expects the LLVM toolchain to be available on the host. If you
build outside the provided Docker devshell, install at least `clang`,
`clang++`, `lld`, and `llvm-ar` before running `make -j$(nproc)`; otherwise the
build will fail with `No such file or directory` errors when invoking the
compiler.

## Troubleshooting

- The CI workflow uploads the serial console output from the smoke test as a
  `qemu-smoke-log` artifact. If a run fails (or if you need to inspect the boot
  sequence), download the artifact from the workflow summary to review the
  captured `build/qemu-smoke.log` file.
- If `scripts/smoke_run.sh` fails with `guest_errors/unimp`, check
  `build/qemu-trace.log` first; this usually indicates QEMU is warning about an
  unimplemented CPU/system feature the kernel touched.
