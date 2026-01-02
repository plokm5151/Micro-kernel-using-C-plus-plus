#pragma once
#include <stddef.h>
#include <stdint.h>

struct Thread {
  void*      sp;         // saved stack pointer (used by arch_switch)
  void     (*entry)(void*);
  void*      arg;
  Thread*    next;       // singly-linked circular runqueue
  int        id;
  void*      stack_base; // for debug (not freed yet)
  size_t     stack_size;
  int        budget;     // remaining time slice (ticks)

  // ---- FPSIMD context ----
  int        fpsimd_valid;                 // 0 = never saved / initial zeros, 1 = valid saved state
  alignas(16) uint8_t fpsimd_vregs[32*16]; // q0..q31 (512 bytes)
  uint64_t   fpcr;                         // FPCR
  uint64_t   fpsr;                         // FPSR
};

#ifdef __cplusplus
// Ensure ctx.S / thread_trampoline can rely on stable field offsets.
static_assert(offsetof(Thread, sp)    == 0,  "Thread::sp must be at +0");
static_assert(offsetof(Thread, entry) == 8,  "Thread::entry must be at +8");
static_assert(offsetof(Thread, arg)   == 16, "Thread::arg must be at +16");
#endif

#ifdef __cplusplus
extern "C" {
#endif
void  sched_init(void);
Thread* thread_create(void (*entry)(void*), void* arg, size_t stack_size);
void  sched_add(Thread* t);
void  sched_start(void);   // enter the first thread (never returns)
void  thread_yield(void);  // cooperative switch to next thread
__attribute__((noreturn)) void thread_exit(void);
void  sched_resched_from_irq_tail(void);
void  sched_on_tick(void);
#ifdef __cplusplus
}
#endif
