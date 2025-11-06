#pragma once

#include <stdint.h>

struct alignas(16) irq_frame {
  uint64_t regs[19];      // x0–x18
  uint64_t lr;            // x30
  uint64_t sp;            // pre-interrupt SP
  uint64_t spsr;          // saved program status
  uint64_t elr;           // return address
  uint64_t reserved;      // keeps the frame 16-byte aligned (future SIMD spill)
};
static_assert(sizeof(struct irq_frame) == 192, "irq_frame size must match assembly");
static_assert(alignof(struct irq_frame) == 16, "irq_frame must be 16-byte aligned");
