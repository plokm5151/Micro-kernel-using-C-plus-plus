#include "thread.h"

#include "arch/ctx.h"
#include "arch/cpu_local.h"
#include "arch/fpsimd.h"
#include "drivers/uart_pl011.h"
#include "kmem.h"

#include <stddef.h>
#include <stdint.h>

#define RR_QUANTUM_TICKS 5

namespace {
Thread* rq_head = nullptr;
Thread* rq_tail = nullptr;
int next_thread_id = 1;

static void do_switch(Thread* cur, Thread* next) {
  auto* cpu = cpu_local();
  // 保存/復原 FPSIMD 狀態（V0–V31 + FPCR/FPSR）
  fpsimd_save(&cur->fp);
  cpu->current_thread = next;
  arch_switch(&cur->sp, next->sp);
  fpsimd_load(&next->fp);
}
}

extern "C" void sched_init(void) {
  rq_head = nullptr;
  rq_tail = nullptr;
  next_thread_id = 1;
}

extern "C" Thread* thread_create(void (*entry)(void*), void* arg, size_t stack_size) {
  uart_puts("[diag] thread_create enter\n");
  if (entry == nullptr || stack_size == 0) {
    uart_puts("[sched][err] invalid thread params\n");
    return nullptr;
  }

  Thread* t = reinterpret_cast<Thread*>(
      kmem_alloc_aligned(sizeof(Thread), alignof(Thread)));
  if (!t) {
    uart_puts("[sched][err] no memory for Thread struct\n");
    return nullptr;
  }

  volatile uint8_t* t_bytes = reinterpret_cast<volatile uint8_t*>(t);
  for (size_t i = 0; i < sizeof(Thread); ++i) {
    t_bytes[i] = 0;
  }

  void* stack = kmem_alloc_aligned(stack_size, 16);
  if (!stack) {
    uart_puts("[sched][err] no memory for thread stack\n");
    return nullptr;
  }

  volatile uint8_t* stack_bytes = reinterpret_cast<volatile uint8_t*>(stack);
  for (size_t i = 0; i < stack_size; ++i) {
    stack_bytes[i] = 0;
  }

  uintptr_t stack_top = reinterpret_cast<uintptr_t>(stack) + stack_size;
  stack_top &= ~static_cast<uintptr_t>(0xF);
  uintptr_t* sp_words = reinterpret_cast<uintptr_t*>(stack_top);
  auto push_pair = [&sp_words](uintptr_t first, uintptr_t second) {
    sp_words -= 2;
    sp_words[0] = first;
    sp_words[1] = second;
  };

  push_pair(0, 0);  // (x19, x20)
  push_pair(0, 0);  // (x21, x22)
  push_pair(0, 0);  // (x23, x24)
  push_pair(0, 0);  // (x25, x26)
  push_pair(0, 0);  // (x27, x28)
  push_pair(0, reinterpret_cast<uintptr_t>(&thread_trampoline)); // (x29, LR)

  t->sp = sp_words;
  t->entry = entry;
  t->arg = arg;
  t->next = nullptr;
  t->id = next_thread_id++;
  t->stack_base = stack;
  t->stack_size = stack_size;
  t->budget = RR_QUANTUM_TICKS;
  // t->fp 已經被上面的清零覆蓋

  uart_puts("[sched][diag] thread created id=");
  uart_print_u64(static_cast<unsigned long long>(t->id));
  uart_puts(" sp=");
  uart_print_u64(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(t->sp)));
  uart_puts(" stack=");
  uart_print_u64(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(t->stack_base)));
  uart_puts("\n");
  uart_puts("[diag] thread_create exit\n");
  return t;
}

extern "C" void sched_add(Thread* t) {
  if (!t) {
    return;
  }
  if (!rq_head) {
    rq_head = t;
    rq_tail = t;
    t->next = t;
    return;
  }
  t->next = rq_head;
  rq_tail->next = t;
  rq_tail = t;
}

extern "C" void sched_start(void) {
  Thread* cur = rq_head;
  if (!cur) {
    uart_puts("[sched] no threads\n");
    while (1) {
      asm volatile("wfe");
    }
  }
  cur->budget = RR_QUANTUM_TICKS;
  cpu_local()->current_thread = cur;
  void* boot_sp = nullptr;
  arch_switch(&boot_sp, cur->sp);
  uart_puts("[BUG] returned to sched_start\n");
  while (1) {
    asm volatile("wfe");
  }
}

extern "C" void thread_yield(void) {
  auto* cpu = cpu_local();
  Thread* cur = cpu->current_thread;
  Thread* next = (cur && cur->next) ? cur->next : cur;
  if (next && next != cur) {
    do_switch(cur, next);
  }
}

extern "C" __attribute__((noreturn)) void thread_exit(void) {
  uart_puts("[thread] exit\n");
  while (1) {
    asm volatile("wfe");
  }
}

extern "C" void sched_on_tick(void) {
  auto* cpu = cpu_local();
  Thread* cur = cpu->current_thread;
  if (!cur) {
    return;
  }
  if (cpu->preempt_cnt) {
    return;
  }
  if (cur->budget > 0) {
    cur->budget--;
  }
  if (cur->budget <= 0) {
    cpu->need_resched = 1;
    cur->budget = RR_QUANTUM_TICKS;
  }
}

extern "C" void sched_resched_from_irq_tail(void) {
  auto* cpu = cpu_local();
  Thread* cur = cpu->current_thread;
  if (!cur) {
    cpu->need_resched = 0;
    return;
  }
  if (cpu->preempt_cnt) {
    return;
  }
  Thread* next = cur->next ? cur->next : cur;
  if (next == cur) {
    cpu->need_resched = 0;
    return;
  }
  do_switch(cur, next);
  cpu->need_resched = 0;
}
