# Micro-kernel-using-C- (AArch64 on QEMU)
- Build toolchain via Docker (LLVM + LLD + QEMU)
- Minimal bare-metal AArch64 kernel (QEMU -machine virt)

## Quick Start
./scripts/devshell.sh
make run

## Host build prerequisites

The `Makefile` expects the LLVM toolchain to be available on the host. If you
build outside the provided Docker devshell, install at least `clang`,
`clang++`, `lld`, and `llvm-ar` before running `make -j$(nproc)`; otherwise the
build will fail with `No such file or directory` errors when invoking the
compiler.
