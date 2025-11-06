#pragma once
#include <stddef.h>
#include <stdint.h>

struct Thread;

struct alignas(64) cpu_local {
  uintptr_t irq_stack_top;   // points to __irq_stack_cpu0_top for CPU0
  Thread*   current_thread;  // 目前執行緒
  unsigned  preempt_cnt;     // preemption nesting counter
  unsigned  need_resched;    // scheduler should pick another thread
  unsigned long ticks;       // timer tick counter
  unsigned  irq_depth;       // nesting depth of active IRQ handlers
} __attribute__((aligned(64)));

static_assert(offsetof(cpu_local, irq_stack_top) == 0, "irq_stack_top must be at offset 0");
#ifdef __cplusplus
extern "C" {
#endif
struct cpu_local* cpu_local(void);   // read TPIDR_EL1
void cpu_local_boot_init(void);      // write TPIDR_EL1 for boot CPU
#ifdef __cplusplus
}
#endif
