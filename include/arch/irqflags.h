#pragma once

static inline unsigned long local_irq_save(void) {
  unsigned long flags;
  asm volatile("mrs %0, daif\n\tmsr daifset, #2" : "=r"(flags) :: "memory");
  return flags;
}

static inline void local_irq_restore(unsigned long flags) {
  asm volatile("msr daif, %0" :: "r"(flags) : "memory");
}
