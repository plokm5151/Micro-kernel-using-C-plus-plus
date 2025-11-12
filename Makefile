CLANG_CANDIDATES   := clang-20 clang-19 clang-18 clang-17 clang-16 clang-15 clang-14 clang
CXX_CANDIDATES     := clang++-20 clang++-19 clang++-18 clang++-17 clang++-16 clang++-15 clang++-14 clang++
LLD_CANDIDATES     := ld.lld-20 ld.lld-19 ld.lld-18 ld.lld-17 ld.lld-16 ld.lld-15 ld.lld-14 ld.lld

find_program = $(firstword $(foreach prog,$(1),$(if $(shell command -v $(prog) >/dev/null 2>&1 && echo 1),$(prog))))

ifeq ($(origin CC), default)
CC := $(call find_program,$(CLANG_CANDIDATES))
endif
ifeq ($(origin CXX), default)
CXX := $(call find_program,$(CXX_CANDIDATES))
endif
ifeq ($(origin LD), default)
LD := $(call find_program,$(LLD_CANDIDATES))
endif

ifeq ($(strip $(CC)),)
$(error Could not find any of the following C compilers: $(CLANG_CANDIDATES))
endif
ifeq ($(strip $(CXX)),)
$(error Could not find any of the following C++ compilers: $(CXX_CANDIDATES))
endif
ifeq ($(strip $(LD)),)
$(error Could not find any of the following linkers: $(LLD_CANDIDATES))
endif

COMMON  := -target aarch64-unknown-none -ffreestanding -fno-stack-protector -O2 -g
CXXFLAGS:= $(COMMON) -fno-exceptions -fno-rtti -Wall -Wextra -DARM_TIMER_DIAG=1 -DUSE_CNTP=0
ASFLAGS := -target aarch64-unknown-none -ffreestanding
LDFLAGS := -nostdlib -static -T boot/kernel.ld

OBJS := \
  build/start.o \
  build/vectors.o \
  build/uart_pl011.o \
  build/gicv3.o \
  build/ctx.o \
  build/cpu_local.o \
  build/barrier.o \
  build/timer.o \
  build/irq.o \
  build/kmem.o \
  build/thread.o \
  build/preempt.o \
  build/dma.o \
  build/except.o \
  build/kmain.o

ELF := build/kernel.elf

all: $(ELF)

build/start.o: boot/start.S
	mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/vectors.o: boot/vectors.S
	mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/uart_pl011.o: src/drivers/uart_pl011.cc src/drivers/uart_pl011.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/gicv3.o: src/arch/aarch64/gicv3.cc include/arch/gicv3.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/cpu_local.o: src/arch/aarch64/cpu_local.cc include/arch/cpu_local.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/ctx.o: src/arch/aarch64/ctx.S include/arch/ctx.h
	mkdir -p build
	$(CC) $(ASFLAGS) -c $< -o $@

build/timer.o: src/arch/aarch64/timer.cc include/arch/timer.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/irq.o: src/irq.cc include/irq.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/kmem.o: src/kmem.cc include/kmem.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/thread.o: src/thread.cc include/thread.h include/arch/ctx.h include/arch/cpu_local.h include/kmem.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -mgeneral-regs-only -Iinclude -Isrc -c $< -o $@

build/preempt.o: src/preempt.cc include/preempt.h include/arch/cpu_local.h include/thread.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/dma.o: src/dma.cc include/dma.h include/arch/barrier.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/kmain.o: src/kmain.cc
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/barrier.o: src/arch/aarch64/barrier.cc include/arch/barrier.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(ELF): $(OBJS) boot/kernel.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $(ELF)

run: $(ELF)
	qemu-system-aarch64 \
	  -machine virt,gic-version=3 \
	  -cpu cortex-a72 \
	  -smp 1 -m 512 \
	  -nographic -serial mon:stdio \
	  -no-reboot -no-shutdown \
	  -kernel $(ELF) \
	  -d guest_errors,unimp -D .qemu.log

clean:
	rm -rf build .qemu.log

.PHONY: all run clean
