CXX     := clang++
CC      := clang
LD      := ld.lld

COMMON  := -target aarch64-unknown-none -ffreestanding -fno-stack-protector -O2 -g
CXXFLAGS:= $(COMMON) -fno-exceptions -fno-rtti
ASFLAGS := -target aarch64-unknown-none -ffreestanding
LDFLAGS := -nostdlib -static -T boot/kernel.ld

OBJS := \
  build/start.o \
  build/vectors.o \
  build/uart_pl011.o \
  build/gicv3.o \
  build/cpu_local.o \
  build/barrier.o \
  build/timer.o \
  build/irq.o \
  build/kmem.o \
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

build/timer.o: src/arch/aarch64/timer.cc include/arch/timer.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/irq.o: src/irq.cc include/irq.h
	mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

build/kmem.o: src/kmem.cc include/kmem.h
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
