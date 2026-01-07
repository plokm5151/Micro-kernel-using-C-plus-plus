#include "thread.h"

#include "arch/ctx.h"
#include "arch/cpu_local.h"
#include "arch/fpsimd.h"
#include "drivers/uart_pl011.h"
#include "kmem.h"

#include <stddef.h>
#include <stdint.h>

namespace {
constexpr int kThreadReady = 0;
constexpr int kThreadBlocked = 1;

constexpr int kDefaultPriority = 10;
constexpr int kMaxPriority = 31;

constexpr int kQuantumTicks = 5;

constexpr uint64_t kStackGuardMagic = 0x737461636b677561ull;  // "stackgua"
constexpr size_t kStackGuardBytes = 64;
constexpr uint8_t kStackWatermark = 0xA5;

constexpr unsigned kNeedReschedNone = 0;
constexpr unsigned kNeedReschedNormal = 1;
#if defined(SCHED_POLICY_PRIO)
constexpr unsigned kNeedReschedRotate = 2;
#endif

#if defined(SCHED_POLICY_RR) && defined(SCHED_POLICY_PRIO)
#error "Define only one of SCHED_POLICY_{RR,PRIO}"
#endif
#if !defined(SCHED_POLICY_RR) && !defined(SCHED_POLICY_PRIO)
#define SCHED_POLICY_RR 1
#endif

Thread* rq_head = nullptr;
Thread* rq_tail = nullptr;
int next_thread_id = 1;

static inline int clamp_priority(int prio) {
  if (prio < 0) return 0;
  if (prio > kMaxPriority) return kMaxPriority;
  return prio;
}

static inline bool is_ready(const Thread* t) {
  return t && t->state == kThreadReady;
}

static void stack_init_guard_and_watermark(void* base, size_t size) {
  if (!base || size == 0) return;
  volatile uint8_t* p = reinterpret_cast<volatile uint8_t*>(base);

  // Guard region: fixed pattern at the bottom of the stack (lowest addresses).
  const size_t guard = (size >= kStackGuardBytes) ? kStackGuardBytes : size;
  for (size_t off = 0; off < guard; off += sizeof(uint64_t)) {
    const size_t remain = guard - off;
    if (remain < sizeof(uint64_t)) {
      for (size_t i = 0; i < remain; ++i) {
        p[off + i] = static_cast<uint8_t>((kStackGuardMagic >> (i * 8u)) & 0xFFu);
      }
    } else {
      *reinterpret_cast<volatile uint64_t*>(reinterpret_cast<volatile void*>(p + off)) = kStackGuardMagic;
    }
  }

  // Watermark: fill the rest of the stack so we can estimate high-water mark.
  for (size_t i = guard; i < size; ++i) {
    p[i] = kStackWatermark;
  }
}

static bool stack_guard_ok(const Thread* t) {
  if (!t || !t->stack_base || t->stack_size == 0) return true;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(t->stack_base);
  const size_t guard = (t->stack_size >= kStackGuardBytes) ? kStackGuardBytes : t->stack_size;

  for (size_t off = 0; off < guard; off += sizeof(uint64_t)) {
    const size_t remain = guard - off;
    if (remain < sizeof(uint64_t)) {
      for (size_t i = 0; i < remain; ++i) {
        const uint8_t expected = static_cast<uint8_t>((kStackGuardMagic >> (i * 8u)) & 0xFFu);
        if (p[off + i] != expected) return false;
      }
    } else {
      const uint64_t v = *reinterpret_cast<const uint64_t*>(p + off);
      if (v != kStackGuardMagic) return false;
    }
  }
  return true;
}

static size_t stack_high_watermark_bytes(const Thread* t) {
  if (!t || !t->stack_base || t->stack_size == 0) return 0;
  if (t->stack_size <= kStackGuardBytes) return t->stack_size;

  const uint8_t* p = reinterpret_cast<const uint8_t*>(t->stack_base);
  size_t unused = 0;
  for (size_t i = kStackGuardBytes; i < t->stack_size; ++i) {
    if (p[i] != kStackWatermark) break;
    unused++;
  }

  const size_t usable = t->stack_size - kStackGuardBytes;
  const size_t used = (unused <= usable) ? (usable - unused) : usable;
  return used;
}

static void rq_append(Thread* t) {
  if (!t) return;
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

static void rq_remove(Thread* t) {
  if (!rq_head || !t) return;
  if (rq_head == t && rq_tail == t) {
    rq_head = nullptr;
    rq_tail = nullptr;
    t->next = nullptr;
    return;
  }

  Thread* prev = rq_head;
  while (prev->next != t && prev->next != rq_head) {
    prev = prev->next;
  }
  if (prev->next != t) {
    return;  // not found
  }

  prev->next = t->next;
  if (rq_head == t) rq_head = t->next;
  if (rq_tail == t) rq_tail = prev;
  t->next = nullptr;
}

#if defined(SCHED_POLICY_PRIO)
static int rq_max_ready_priority() {
  if (!rq_head) return -1;
  int best = -1;
  Thread* start = rq_head;
  Thread* t = start;
  do {
    if (is_ready(t) && t->effective_priority > best) {
      best = t->effective_priority;
    }
    t = t->next;
  } while (t && t != start);
  return best;
}

static bool rq_has_ready_prio_gt(int prio) {
  if (!rq_head) return false;
  Thread* start = rq_head;
  Thread* t = start;
  do {
    if (is_ready(t) && t->effective_priority > prio) {
      return true;
    }
    t = t->next;
  } while (t && t != start);
  return false;
}

static Thread* prio_pick_next(Thread* cur, bool exclude_current, bool rotate_equal) {
  if (!rq_head) return cur;

  const int best_prio = rq_max_ready_priority();
  if (best_prio < 0) return cur;

  if (!exclude_current && !rotate_equal && cur && is_ready(cur) && cur->effective_priority == best_prio) {
    return cur;
  }

  Thread* start = rq_head;
  if ((exclude_current || rotate_equal) && cur && cur->next) {
    start = cur->next;
  }

  Thread* t = start;
  do {
    if (is_ready(t) && t->effective_priority == best_prio && (!exclude_current || t != cur)) {
      return t;
    }
    t = t->next;
  } while (t && t != start);

  if (cur && is_ready(cur) && cur->effective_priority == best_prio) {
    return cur;
  }
  return rq_head;
}
#endif  // SCHED_POLICY_PRIO

static void do_switch(Thread* cur, Thread* next) {
  auto* cpu = cpu_local();
  // Use the no-argument FPSIMD API (state is read/written via cpu_local()->current_thread).
  // Save current thread FPSIMD, switch current_thread, then restore next thread FPSIMD.
  fpsimd_save();
  cpu->current_thread = next;
  arch_switch(&cur->sp, next->sp);
  fpsimd_load();
}
}

extern "C" void sched_init(void) {
  rq_head = nullptr;
  rq_tail = nullptr;
  next_thread_id = 1;
}

extern "C" Thread* thread_create(void (*entry)(void*), void* arg, size_t stack_size) {
  return thread_create_prio(entry, arg, stack_size, kDefaultPriority);
}

extern "C" Thread* thread_create_prio(void (*entry)(void*), void* arg, size_t stack_size, int base_priority) {
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
  stack_init_guard_and_watermark(stack, stack_size);

  uintptr_t stack_top = reinterpret_cast<uintptr_t>(stack) + stack_size;
  stack_top &= ~static_cast<uintptr_t>(0xF);
  uintptr_t* sp_words = reinterpret_cast<uintptr_t*>(stack_top);
  auto push_pair = [&sp_words](uintptr_t first, uintptr_t second) {
    sp_words -= 2;
    sp_words[0] = first;
    sp_words[1] = second;
  };

  // Matches arch_switch save/restore set: x19..x30.
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
  t->budget = kQuantumTicks;

  t->base_priority = clamp_priority(base_priority);
  t->effective_priority = t->base_priority;
  t->state = kThreadReady;
  t->wait_next = nullptr;
  t->owned_mutexes = nullptr;
  // FPSIMD state was zeroed above: fpsimd_valid=0, vregs=0, fpcr/fpsr=0.

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
  if (t->state != kThreadReady) {
    t->state = kThreadReady;
  }
  rq_append(t);
}

extern "C" void sched_start(void) {
  Thread* cur = rq_head;
#if defined(SCHED_POLICY_PRIO)
  cur = prio_pick_next(nullptr, /*exclude_current=*/false, /*rotate_equal=*/false);
#endif
  if (!cur) {
    uart_puts("[sched] no threads\n");
    while (1) {
      asm volatile("wfe");
    }
  }
  cur->budget = kQuantumTicks;
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
  if (!cpu || cpu->preempt_cnt) {
    return;
  }
  Thread* cur = cpu->current_thread;
  if (!cur) return;

#if defined(SCHED_POLICY_PRIO)
  Thread* next = prio_pick_next(cur, /*exclude_current=*/true, /*rotate_equal=*/true);
  if (next && next != cur) {
    do_switch(cur, next);
  }
#else
  Thread* next = (cur && cur->next) ? cur->next : cur;
  if (next && next != cur) {
    do_switch(cur, next);
  }
#endif
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
  if (!stack_guard_ok(cur)) {
    uart_puts("[stack] overflow detected tid=");
    uart_print_u64(static_cast<unsigned long long>(cur->id));
    uart_puts("\n");
    while (1) {
      asm volatile("wfe");
    }
  }
  if (cpu->preempt_cnt) {
    return;
  }

#if defined(SCHED_POLICY_PRIO)
  if (!is_ready(cur)) {
    cpu->need_resched = kNeedReschedNormal;
    return;
  }
  if (rq_has_ready_prio_gt(cur->effective_priority)) {
    cpu->need_resched = kNeedReschedNormal;
    return;
  }
#endif

  if (cur->budget > 0) {
    cur->budget--;
  }
  if (cur->budget <= 0) {
  #if defined(SCHED_POLICY_PRIO)
    cpu->need_resched = kNeedReschedRotate;
  #else
    cpu->need_resched = kNeedReschedNormal;
  #endif
    cur->budget = kQuantumTicks;
  }
}

extern "C" void sched_resched_from_irq_tail(void) {
  auto* cpu = cpu_local();
  Thread* cur = cpu->current_thread;
  if (cpu->preempt_cnt) {
    return;
  }

  Thread* next = nullptr;
#if defined(SCHED_POLICY_PRIO)
  const bool rotate = (cpu->need_resched == kNeedReschedRotate);
  next = prio_pick_next(cur, /*exclude_current=*/rotate, /*rotate_equal=*/rotate);
#else
  if (cur && is_ready(cur) && cur->next) {
    next = cur->next;
  } else {
    next = rq_head;
  }
#endif

  if (!cur || !next || next == cur) {
    cpu->need_resched = kNeedReschedNone;
    return;
  }

  do_switch(cur, next);
  cpu->need_resched = kNeedReschedNone;
}

extern "C" void sched_block_current(void) {
  auto* cpu = cpu_local();
  Thread* cur = cpu ? cpu->current_thread : nullptr;
  if (!cur) return;
  if (cur->state == kThreadBlocked) return;
  rq_remove(cur);
  cur->state = kThreadBlocked;
  cur->wait_next = nullptr;
}

extern "C" void sched_make_runnable(Thread* t) {
  if (!t) return;
  if (t->state == kThreadReady) return;
  t->state = kThreadReady;
  t->wait_next = nullptr;
  t->budget = kQuantumTicks;
  rq_append(t);
}

extern "C" int thread_base_priority(const Thread* t) {
  return t ? t->base_priority : 0;
}

extern "C" int thread_effective_priority(const Thread* t) {
  return t ? t->effective_priority : 0;
}

extern "C" void thread_set_base_priority(Thread* t, int prio) {
  if (!t) return;
  int p = clamp_priority(prio);
  t->base_priority = p;
  if (t->effective_priority < p) {
    t->effective_priority = p;
  }
}

extern "C" void thread_set_effective_priority(Thread* t, int prio) {
  if (!t) return;
  int p = clamp_priority(prio);
  if (p < t->base_priority) {
    p = t->base_priority;
  }
  t->effective_priority = p;
}

extern "C" int thread_stack_guard_ok(const Thread* t) {
  return stack_guard_ok(t) ? 1 : 0;
}

extern "C" size_t thread_stack_high_watermark_bytes(const Thread* t) {
  return stack_high_watermark_bytes(t);
}
