#include "sync.h"

#include "arch/cpu_local.h"
#include "drivers/uart_pl011.h"
#include "preempt.h"

namespace {
#ifndef SYNC_LAB_MODE
#define SYNC_LAB_MODE 0
#endif

#ifndef MUTEX_PI
#define MUTEX_PI 1
#endif

#if SYNC_LAB_MODE == 5
#define LOCKDEP_ENABLED 1
#else
#define LOCKDEP_ENABLED 0
#endif

#if LOCKDEP_ENABLED
static void lockdep_panic_deadlock(Thread* cur, mutex* m) {
  uart_puts("[lockdep] deadlock cycle detected: tid=");
  uart_print_u64(static_cast<unsigned long long>(cur ? cur->id : -1));
  uart_puts(" waiting_on=0x");
  uart_print_u64(static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(m)));
  uart_puts("\n");
  uart_puts("[lockdep] result PASS\n");
  while (1) {
    asm volatile("wfe");
  }
}

static bool lockdep_would_deadlock(Thread* cur, mutex* m) {
  if (!cur || !m) return false;
  Thread* owner = m->owner;
  // Walk the owner -> waiting_on -> owner chain looking for a cycle to |cur|.
  // This is sufficient to catch classic AB/BA deadlocks and small cycles.
  for (unsigned depth = 0; owner && depth < 16u; ++depth) {
    if (owner == cur) return true;
    mutex* wait = owner->waiting_on;
    if (!wait) break;
    owner = wait->owner;
  }
  return false;
}
#endif

static Thread* waitq_pop_highest(Thread** head) {
  if (!head || !*head) return nullptr;

  Thread* best_prev = nullptr;
  Thread* best = *head;
  int best_prio = thread_effective_priority(best);

  Thread* prev = nullptr;
  for (Thread* t = *head; t; t = t->wait_next) {
    int prio = thread_effective_priority(t);
    if (prio > best_prio) {
      best_prio = prio;
      best_prev = prev;
      best = t;
    }
    prev = t;
  }

  if (best_prev) {
    best_prev->wait_next = best->wait_next;
  } else {
    *head = best->wait_next;
  }
  best->wait_next = nullptr;
  return best;
}

static int waitq_max_priority(Thread* head) {
  int best = -1;
  for (Thread* t = head; t; t = t->wait_next) {
    int prio = thread_effective_priority(t);
    if (prio > best) best = prio;
  }
  return best;
}

static void thread_owned_mutex_add(Thread* t, mutex* m) {
  if (!t || !m) return;
  m->owner_next = t->owned_mutexes;
  t->owned_mutexes = m;
}

static void thread_owned_mutex_remove(Thread* t, mutex* m) {
  if (!t || !m) return;
  mutex* prev = nullptr;
  for (mutex* it = t->owned_mutexes; it; it = it->owner_next) {
    if (it == m) {
      if (prev) {
        prev->owner_next = it->owner_next;
      } else {
        t->owned_mutexes = it->owner_next;
      }
      it->owner_next = nullptr;
      return;
    }
    prev = it;
  }
}

static void recompute_effective_priority(Thread* t) {
  if (!t) return;
  int eff = thread_base_priority(t);
#if MUTEX_PI
  for (mutex* m = t->owned_mutexes; m; m = m->owner_next) {
    if (!m->pi_enabled) continue;
    int w = waitq_max_priority(m->waiters);
    if (w > eff) eff = w;
  }
#endif
  thread_set_effective_priority(t, eff);
}

static void mutex_apply_pi(mutex* m) {
  if (!m || !m->pi_enabled) return;
  if (!m->owner) return;
  recompute_effective_priority(m->owner);
}
}  // namespace

extern "C" void mutex_init(mutex* m) {
  if (!m) return;
  m->owner = nullptr;
  m->waiters = nullptr;
  m->owner_next = nullptr;
  m->pi_enabled = MUTEX_PI ? 1 : 0;
}

extern "C" void mutex_set_pi_enabled(mutex* m, int enabled) {
  if (!m) return;
  preempt_disable();
  m->pi_enabled = enabled ? 1 : 0;
  if (m->owner) {
    recompute_effective_priority(m->owner);
  }
  preempt_enable();
}

extern "C" void mutex_lock(mutex* m) {
  if (!m) return;

  for (;;) {
    preempt_disable();
    auto* cpu = cpu_local();
    Thread* cur = cpu ? cpu->current_thread : nullptr;
    if (!cur) {
      preempt_enable();
      return;
    }

    if (m->owner == cur) {
      // Already the owner (non-recursive mutex); treat as acquired.
      cur->waiting_on = nullptr;
      preempt_enable();
      return;
    }

    if (m->owner == nullptr) {
      m->owner = cur;
      thread_owned_mutex_add(cur, m);
      cur->waiting_on = nullptr;
      preempt_enable();
      return;
    }

#if LOCKDEP_ENABLED
    if (lockdep_would_deadlock(cur, m)) {
      lockdep_panic_deadlock(cur, m);
    }
#endif

    // Block.
    cur->waiting_on = m;
    cur->wait_next = m->waiters;
    m->waiters = cur;
    mutex_apply_pi(m);

    sched_block_current();
    cpu->need_resched = 1;
    preempt_enable();
  }
}

extern "C" int mutex_trylock(mutex* m) {
  if (!m) return -1;

  preempt_disable();
  auto* cpu = cpu_local();
  Thread* cur = cpu ? cpu->current_thread : nullptr;
  if (!cur) {
    preempt_enable();
    return -1;
  }

  if (m->owner == nullptr) {
    m->owner = cur;
    thread_owned_mutex_add(cur, m);
    cur->waiting_on = nullptr;
    preempt_enable();
    return 0;
  }

  if (m->owner == cur) {
    cur->waiting_on = nullptr;
    preempt_enable();
    return 0;
  }

  preempt_enable();
  return -1;
}

extern "C" void mutex_unlock(mutex* m) {
  if (!m) return;

  preempt_disable();
  auto* cpu = cpu_local();
  Thread* cur = cpu ? cpu->current_thread : nullptr;
  if (!cur || m->owner != cur) {
    preempt_enable();
    return;
  }

  thread_owned_mutex_remove(cur, m);

  Thread* next_owner = waitq_pop_highest(&m->waiters);
  if (next_owner) {
    m->owner = next_owner;
    next_owner->waiting_on = nullptr;
    thread_owned_mutex_add(next_owner, m);
    sched_make_runnable(next_owner);
    mutex_apply_pi(m);
  } else {
    m->owner = nullptr;
  }

  recompute_effective_priority(cur);
  if (next_owner && cpu && cpu->current_thread == cur &&
      thread_effective_priority(next_owner) > thread_effective_priority(cur)) {
    cpu->need_resched = 1;
  }
  preempt_enable();
}

extern "C" void sem_init(semaphore* s, int initial_count) {
  if (!s) return;
  s->count = initial_count;
  s->waiters = nullptr;
}

extern "C" void sem_down(semaphore* s) {
  if (!s) return;
  preempt_disable();
  s->count--;
  if (s->count >= 0) {
    preempt_enable();
    return;
  }

  auto* cpu = cpu_local();
  Thread* cur = cpu ? cpu->current_thread : nullptr;
  if (!cur) {
    preempt_enable();
    return;
  }

  cur->waiting_on = nullptr;
  cur->wait_next = s->waiters;
  s->waiters = cur;
  sched_block_current();
  cpu->need_resched = 1;
  preempt_enable();
}

extern "C" void sem_up(semaphore* s) {
  if (!s) return;
  preempt_disable();

  s->count++;
  if (s->count <= 0) {
    Thread* t = waitq_pop_highest(&s->waiters);
    if (t) {
      sched_make_runnable(t);
      auto* cpu = cpu_local();
      if (cpu && cpu->current_thread &&
          thread_effective_priority(t) > thread_effective_priority(cpu->current_thread)) {
        cpu->need_resched = 1;
      }
    }
  }

  preempt_enable();
}
