# Micro-kernel-using-C- (AArch64 on QEMU)
- Build toolchain via Docker (LLVM + LLD + QEMU)
- Minimal bare-metal AArch64 kernel (QEMU -machine virt)

## Quick Start
./scripts/devshell.sh
make run

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
