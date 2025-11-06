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
  non-cacheable DMA buffers. The 4 KiB alignment matches page granularity,
  simplifying future cache maintenance and memory attribute updates.

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
