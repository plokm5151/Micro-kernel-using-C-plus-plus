#include "arch/cpu_local.h"

static_assert(alignof(struct cpu_local) == 64, "cpu_local must remain 64-byte aligned");
extern "C" struct cpu_local* cpu_local() {
  uintptr_t p=0; asm volatile("mrs %0, tpidr_el1":"=r"(p));
  return (struct cpu_local*)p;
}
extern "C" void cpu_local_boot_init(void) __attribute__((target("arch=armv8-a+nosimd")));
extern "C" void cpu_local_boot_init(void) {
  static struct cpu_local cpu0;
  extern char __irq_stack_cpu0_top[];
  cpu0.irq_stack_top = (uintptr_t)__irq_stack_cpu0_top;
  cpu0.current_thread = nullptr;
  cpu0.preempt_cnt = 0u;
  cpu0.need_resched = 0u;
  cpu0.ticks = 0ul;
  cpu0.irq_depth = 0u;
  uintptr_t p = (uintptr_t)&cpu0;
  asm volatile("msr tpidr_el1, %0" :: "r"(p));
  asm volatile("isb");
}
