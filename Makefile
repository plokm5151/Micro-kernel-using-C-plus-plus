CLANG_CANDIDATES   := clang-20 clang-19 clang-18 clang-17 clang-16 clang-15 clang-14 clang
CXX_CANDIDATES     := clang++-20 clang++-19 clang++-18 clang++-17 clang++-16 clang++-15 clang++-14 clang++
LLD_CANDIDATES     := ld.lld-20 ld.lld-19 ld.lld-18 ld.lld-17 ld.lld-16 ld.lld-15 ld.lld-14 ld.lld
OBJCOPY_CANDIDATES := llvm-objcopy objcopy

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
ifeq ($(origin OBJCOPY), default)
OBJCOPY := $(call find_program,$(OBJCOPY_CANDIDATES))
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

OBJ_DIR ?= build
START_SRC ?= boot/start.S
LDSCRIPT ?= boot/kernel.ld

COMMON  := -target aarch64-unknown-none -ffreestanding -fno-stack-protector -O2 -g
# Temporarily disable auto-vectorization (remove once FPSIMD context is fully validated).
CXXFLAGS:= $(COMMON) -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -DARM_TIMER_DIAG=1 -DUSE_CNTP=0 -fno-vectorize -fno-slp-vectorize
ASFLAGS := -target aarch64-unknown-none -ffreestanding
LDFLAGS := -nostdlib -static -T $(LDSCRIPT)

# DMA window mapping policy (default: CACHEABLE so coherency bugs surface).
DMA_WINDOW_POLICY ?= CACHEABLE
DMA_LAB_MODE ?= 0

# Scheduler policy (default: RR to preserve CI behavior).
SCHED_POLICY ?= RR

# Enable priority inheritance for mutexes by default.
MUTEX_PI ?= 1

# Synchronization/scheduler lab mode (default: off).
SYNC_LAB_MODE ?= 0

# Memory allocator / fragmentation lab mode (default: off).
MEM_LAB_MODE ?= 0

# Platform selection.
# - virt: QEMU -machine virt (default, used by CI smoke test)
# - rpi4: Raspberry Pi 4 (AArch64 firmware-loaded kernel8.img)
PLATFORM ?= virt

ifeq ($(PLATFORM),virt)
CXXFLAGS += -DPLATFORM_VIRT=1
PLATFORM_SRC := src/platform/virt.cc
MMU_SRC := src/arch/aarch64/mmu.cc
else ifeq ($(PLATFORM),rpi4)
CXXFLAGS += -DPLATFORM_RPI4=1
PLATFORM_SRC := src/platform/rpi4.cc
MMU_SRC := src/arch/aarch64/mmu_rpi4.cc
else
$(error PLATFORM must be virt or rpi4)
endif

ifeq ($(PLATFORM),rpi4)
ifneq ($(strip $(RPI4_UART_CLOCK_HZ)),)
CXXFLAGS += -DRPI4_UART_CLOCK_HZ=$(RPI4_UART_CLOCK_HZ)
endif
endif

ifeq ($(DMA_WINDOW_POLICY),CACHEABLE)
CXXFLAGS += -DDMA_WINDOW_POLICY_CACHEABLE=1
else ifeq ($(DMA_WINDOW_POLICY),NONCACHEABLE)
CXXFLAGS += -DDMA_WINDOW_POLICY_NONCACHEABLE=1
else
$(error DMA_WINDOW_POLICY must be CACHEABLE or NONCACHEABLE)
endif

CXXFLAGS += -DDMA_LAB_MODE=$(DMA_LAB_MODE)

ifeq ($(SCHED_POLICY),RR)
CXXFLAGS += -DSCHED_POLICY_RR=1
else ifeq ($(SCHED_POLICY),PRIO)
CXXFLAGS += -DSCHED_POLICY_PRIO=1
else
$(error SCHED_POLICY must be RR or PRIO)
endif

CXXFLAGS += -DMUTEX_PI=$(MUTEX_PI)
CXXFLAGS += -DSYNC_LAB_MODE=$(SYNC_LAB_MODE)
CXXFLAGS += -DMEM_LAB_MODE=$(MEM_LAB_MODE)

OBJS := \
  $(OBJ_DIR)/start.o \
  $(OBJ_DIR)/vectors.o \
  $(OBJ_DIR)/uart_pl011.o \
  $(OBJ_DIR)/platform.o \
  $(OBJ_DIR)/gicv3.o \
  $(OBJ_DIR)/ctx.o \
  $(OBJ_DIR)/cpu_local.o \
  $(OBJ_DIR)/barrier.o \
  $(OBJ_DIR)/mmu.o \
  $(OBJ_DIR)/timer.o \
  $(OBJ_DIR)/irq.o \
  $(OBJ_DIR)/libc.o \
  $(OBJ_DIR)/kmem.o \
  $(OBJ_DIR)/mem_pool.o \
  $(OBJ_DIR)/mem_lab.o \
  $(OBJ_DIR)/sync.o \
  $(OBJ_DIR)/thread.o \
  $(OBJ_DIR)/preempt.o \
  $(OBJ_DIR)/dma.o \
  $(OBJ_DIR)/dma_lab.o \
  $(OBJ_DIR)/sync_lab.o \
  $(OBJ_DIR)/except.o \
  $(OBJ_DIR)/fpsimd.o \
  $(OBJ_DIR)/kmain.o

ELF ?= $(OBJ_DIR)/kernel.elf

all: $(ELF)

$(OBJ_DIR)/start.o: $(START_SRC)
	mkdir -p $(OBJ_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OBJ_DIR)/vectors.o: boot/vectors.S
	mkdir -p $(OBJ_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OBJ_DIR)/uart_pl011.o: src/drivers/uart_pl011.cc src/drivers/uart_pl011.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/platform.o: $(PLATFORM_SRC) include/platform.h src/drivers/uart_pl011.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/gicv3.o: src/arch/aarch64/gicv3.cc include/arch/gicv3.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/cpu_local.o: src/arch/aarch64/cpu_local.cc include/arch/cpu_local.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/ctx.o: src/arch/aarch64/ctx.S include/arch/ctx.h
	mkdir -p $(OBJ_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(OBJ_DIR)/timer.o: src/arch/aarch64/timer.cc include/arch/timer.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/irq.o: src/irq.cc include/irq.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/libc.o: src/libc.cc
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/kmem.o: src/kmem.cc include/kmem.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/mem_pool.o: src/mem_pool.cc include/mem_pool.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/mem_lab.o: src/mem_lab.cc include/mem_lab.h include/mem_pool.h include/kmem.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/sync.o: src/sync.cc include/sync.h include/thread.h include/arch/cpu_local.h include/preempt.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/thread.o: src/thread.cc include/thread.h include/arch/ctx.h include/arch/cpu_local.h include/kmem.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -mgeneral-regs-only -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/preempt.o: src/preempt.cc include/preempt.h include/arch/cpu_local.h include/thread.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/dma.o: src/dma.cc include/dma.h include/arch/barrier.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/dma_lab.o: src/dma_lab.cc include/dma_lab.h include/dma.h include/kmem.h include/arch/barrier.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/sync_lab.o: src/sync_lab.cc include/sync_lab.h include/sync.h include/thread.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/except.o: src/arch/aarch64/except.cc include/arch/except.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

# NEW: FPSIMD
$(OBJ_DIR)/fpsimd.o: src/arch/aarch64/fpsimd.cc include/arch/fpsimd.h include/arch/cpu_local.h include/thread.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/kmain.o: src/kmain.cc
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/barrier.o: src/arch/aarch64/barrier.cc include/arch/barrier.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(OBJ_DIR)/mmu.o: $(MMU_SRC) include/arch/mmu.h
	mkdir -p $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -Iinclude -Isrc -c $< -o $@

$(ELF): $(OBJS) $(LDSCRIPT)
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

.PHONY: all run clean rpi4

RPI4_ELF := build/kernel_rpi4.elf
RPI4_IMG := build/kernel8.img

rpi4:
	$(MAKE) PLATFORM=rpi4 OBJ_DIR=build/rpi4 START_SRC=boot/rpi4_start.S LDSCRIPT=boot/kernel_rpi4.ld ELF=$(RPI4_ELF) all
	@if [ -z "$(OBJCOPY)" ]; then echo "Could not find objcopy tool (tried: $(OBJCOPY_CANDIDATES))"; exit 2; fi
	$(OBJCOPY) -O binary $(RPI4_ELF) $(RPI4_IMG)
	@echo "[rpi4] Wrote $(RPI4_IMG) (ELF: $(RPI4_ELF))"
