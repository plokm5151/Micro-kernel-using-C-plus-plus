#pragma once
#include <stddef.h>
#include <stdint.h>

struct alignas(64) cpu_local {
  uintptr_t irq_stack_top;   // points to __irq_stack_cpu0_top for CPU0
  uintptr_t reserved[7];
  // reserved: current_thread, preempt_cnt, cpu_id ...
};

static_assert(offsetof(cpu_local, irq_stack_top) == 0, "irq_stack_top must be at offset 0");
#ifdef __cplusplus
extern "C" {
#endif
struct cpu_local* cpu_local(void);   // read TPIDR_EL1
void cpu_local_boot_init(void);      // write TPIDR_EL1 for boot CPU
#ifdef __cplusplus
}
#endif
