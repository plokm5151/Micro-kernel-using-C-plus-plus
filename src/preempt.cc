#include "preempt.h"

#include "arch/cpu_local.h"
#include "thread.h"

extern "C" void preempt_disable(void) {
  auto* cpu = cpu_local();
  cpu->preempt_cnt++;
  __asm__ __volatile__("" ::: "memory");
}

extern "C" void preempt_enable(void) {
  auto* cpu = cpu_local();
  __asm__ __volatile__("" ::: "memory");
  if (cpu->preempt_cnt == 0) {
    return;
  }
  cpu->preempt_cnt--;
  if (cpu->preempt_cnt == 0 && cpu->need_resched && cpu->irq_depth == 0) {
    sched_resched_from_irq_tail();
  }
}

extern "C" void preempt_return(void) {
  auto* cpu = cpu_local();
  if (cpu->preempt_cnt == 0 && cpu->need_resched) {
    sched_resched_from_irq_tail();
  }
}
