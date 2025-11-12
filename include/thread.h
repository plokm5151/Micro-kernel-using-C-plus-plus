#pragma once
#include <stddef.h>
#include <stdint.h>
#include "arch/fpsimd.h"

struct Thread {
  void*      sp;         // 保存的 stack pointer（arch_switch 用）
  void     (*entry)(void*);
  void*      arg;
  Thread*    next;       // 單向循環佇列
  int        id;
  void*      stack_base; // for free/debug (暫不釋放)
  size_t     stack_size;
  int        budget;     // 剩餘時間片（tick 數）
  struct fpsimd_state fp; // 每個 thread 的 FPSIMD 狀態
};

#ifdef __cplusplus
extern "C" {
#endif
void  sched_init(void);
Thread* thread_create(void (*entry)(void*), void* arg, size_t stack_size);
void  sched_add(Thread* t);
void  sched_start(void);   // 切入第一個 thread（不可返回）
void  thread_yield(void);  // cooperative 切換至下一個
__attribute__((noreturn)) void thread_exit(void);
void  sched_resched_from_irq_tail(void);
void  sched_on_tick(void);
#ifdef __cplusplus
}
#endif
