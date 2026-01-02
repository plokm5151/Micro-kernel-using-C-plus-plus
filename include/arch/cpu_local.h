#pragma once
#include <stddef.h>
#include <stdint.h>

struct Thread;

struct alignas(64) cpu_local {
  uintptr_t irq_stack_top;   // points to __irq_stack_cpu0_top for CPU0
  Thread*   current_thread;  // currently running thread
  unsigned  preempt_cnt;     // preemption nesting counter
  unsigned  need_resched;    // scheduler should pick another thread
  unsigned long ticks;       // timer tick counter
  unsigned  irq_depth;       // nesting depth of active IRQ handlers
} __attribute__((aligned(64)));
#ifdef __cplusplus
static_assert(offsetof(struct cpu_local, irq_stack_top) == 0,
              "cpu_local.irq_stack_top at offset 0");
static_assert(sizeof(struct cpu_local) % 64 == 0,
              "cpu_local aligned to 64B");
#endif
#ifdef __cplusplus
extern "C" {
#endif
struct cpu_local* cpu_local(void);   // read TPIDR_EL1
void cpu_local_boot_init(void);      // write TPIDR_EL1 for boot CPU
#ifdef __cplusplus
}
#endif
