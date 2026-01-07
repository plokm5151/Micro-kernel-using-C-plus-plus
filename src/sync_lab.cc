#include "sync_lab.h"

#include <stdint.h>

#include "arch/cpu_local.h"
#include "drivers/uart_pl011.h"
#include "preempt.h"
#include "sync.h"
#include "thread.h"

namespace {
mutex g_lock;
semaphore g_start;
semaphore g_hold_high;

volatile int g_pi_enabled = 0;
volatile int g_high_done = 0;

// Deadlock lab state (2 locks / 2 threads + watchdog).
mutex g_dl_a;
mutex g_dl_b;
semaphore g_dl_hold;
volatile unsigned g_dl_ready = 0;
volatile int g_dl_done = 0;
volatile unsigned g_dl_mode = 0;
Thread* g_dl_t1 = nullptr;
Thread* g_dl_t2 = nullptr;

static inline void spin(unsigned n) {
  for (volatile unsigned i = 0; i < n; ++i) {
    asm volatile("" ::: "memory");
  }
}

static void low_thread(void*) {
  uart_puts("[sync-lab] L start\n");
  mutex_lock(&g_lock);
  uart_puts("[sync-lab] L locked\n");

  // Release H and M.
  sem_up(&g_start);
  sem_up(&g_start);

  // Hold the lock until PI is enabled (to create a deterministic inversion window).
  while (!g_pi_enabled) {
    spin(20000);
  }

  uart_puts("[sync-lab] L unlocking\n");
  mutex_unlock(&g_lock);
  uart_puts("[sync-lab] L unlocked\n");

  while (1) {
    asm volatile("wfe");
  }
}

static void high_thread(void*) {
  uart_puts("[sync-lab] H start\n");
  sem_down(&g_start);

  uart_puts("[sync-lab] H lock...\n");
  mutex_lock(&g_lock);
  uart_puts("[sync-lab] H acquired\n");
  g_high_done = 1;
  mutex_unlock(&g_lock);
  uart_puts("[sync-lab] H done\n");

  // In a strict priority scheduler, H would otherwise remain runnable and
  // starve M from printing the final PASS line. Block H permanently.
  sem_down(&g_hold_high);
}

static void medium_thread(void*) {
  uart_puts("[sync-lab] M start\n");
  sem_down(&g_start);

  uint64_t t0 = cpu_local()->ticks;
  bool printed = false;
  while (!g_high_done) {
    spin(80000);

    uint64_t dt = cpu_local()->ticks - t0;
    if (!g_pi_enabled && dt >= 20) {
      if (!printed) {
        uart_puts("[sync-lab] inversion observed; enabling PI\n");
        printed = true;
      }
      g_pi_enabled = 1;
      mutex_set_pi_enabled(&g_lock, 1);
      thread_yield();
    }
  }

  uart_puts("[sync-lab] result PASS\n");
  while (1) {
    asm volatile("wfe");
  }
}

static void dl_lock_two_ordered(mutex* a, mutex* b) {
  if (!a || !b) return;
  if (a == b) {
    mutex_lock(a);
    return;
  }
  if (reinterpret_cast<uintptr_t>(a) < reinterpret_cast<uintptr_t>(b)) {
    mutex_lock(a);
    mutex_lock(b);
  } else {
    mutex_lock(b);
    mutex_lock(a);
  }
}

static void dl_unlock_two_ordered(mutex* a, mutex* b) {
  if (!a || !b) return;
  if (a == b) {
    mutex_unlock(a);
    return;
  }
  // Unlock in reverse order for symmetry.
  if (reinterpret_cast<uintptr_t>(a) < reinterpret_cast<uintptr_t>(b)) {
    mutex_unlock(b);
    mutex_unlock(a);
  } else {
    mutex_unlock(a);
    mutex_unlock(b);
  }
}

static void dl_lock_two_trylock_backoff(mutex* first, mutex* second, unsigned backoff_base) {
  unsigned attempt = 0;
  for (;;) {
    mutex_lock(first);
    if (mutex_trylock(second) == 0) {
      return;
    }
    mutex_unlock(first);

    // Break symmetry deterministically to avoid livelock.
    attempt++;
    spin(backoff_base + (attempt * 2000u));
    thread_yield();
  }
}

static void deadlock_worker(void* arg) {
  const unsigned tid = static_cast<unsigned>(reinterpret_cast<uintptr_t>(arg));
  uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" start\n");

  mutex* first = (tid == 1) ? &g_dl_a : &g_dl_b;
  mutex* second = (tid == 1) ? &g_dl_b : &g_dl_a;

  if (g_dl_mode == 2u) {
    // Naive AB/BA acquisition: expected to deadlock.
    mutex_lock(first);
    uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" locked first\n");
    preempt_disable();
    g_dl_ready++;
    preempt_enable();
    while (g_dl_ready < 2u) {
      spin(20000);
      thread_yield();
    }
    uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" locking second...\n");
    mutex_lock(second);
    uart_puts("[deadlock-lab] BUG: acquired second (unexpected)\n");
  } else if (g_dl_mode == 3u) {
    // Fix #1: global lock ordering.
    dl_lock_two_ordered(&g_dl_a, &g_dl_b);
    uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" acquired both (ordered)\n");
    dl_unlock_two_ordered(&g_dl_a, &g_dl_b);
    preempt_disable();
    g_dl_done++;
    preempt_enable();
    sem_down(&g_dl_hold);
  } else if (g_dl_mode == 4u) {
    // Fix #2: trylock + backoff.
    const unsigned backoff = (tid == 1) ? 3000u : 7000u;
    dl_lock_two_trylock_backoff(first, second, backoff);
    uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" acquired both (trylock)\n");
    mutex_unlock(second);
    mutex_unlock(first);
    preempt_disable();
    g_dl_done++;
    preempt_enable();
    sem_down(&g_dl_hold);
  } else if (g_dl_mode == 5u) {
    // Fix #3: simplified lockdep (detected inside mutex_lock).
    mutex_lock(first);
    uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" locked first\n");
    preempt_disable();
    g_dl_ready++;
    preempt_enable();
    while (g_dl_ready < 2u) {
      spin(20000);
      thread_yield();
    }
    uart_puts("[deadlock-lab] T"); uart_print_u64(tid); uart_puts(" locking second...\n");
    mutex_lock(second);  // Expected to trigger lockdep and halt.
    uart_puts("[deadlock-lab] BUG: lockdep did not trigger\n");
  } else {
    uart_puts("[deadlock-lab] invalid mode\n");
  }

  while (1) {
    asm volatile("wfe");
  }
}

static int dl_deadlock_observed() {
  if (!g_dl_t1 || !g_dl_t2) return 0;
  if (g_dl_t1->state != 1 || g_dl_t2->state != 1) return 0;  // both BLOCKED
  if (g_dl_a.owner != g_dl_t1 || g_dl_b.owner != g_dl_t2) return 0;
  if (g_dl_t1->waiting_on != &g_dl_b) return 0;
  if (g_dl_t2->waiting_on != &g_dl_a) return 0;
  return 1;
}

static void deadlock_watchdog(void*) {
  uart_puts("[deadlock-lab] watchdog start\n");
  uint64_t t0 = cpu_local()->ticks;
  bool printed = false;

  for (;;) {
    const uint64_t dt = cpu_local()->ticks - t0;

    if (g_dl_mode == 2u) {
      if (dl_deadlock_observed()) {
        uart_puts("[deadlock-lab] deadlock observed (AB/BA)\n");
        uart_puts("[deadlock-lab] fixes: lock ordering | trylock+backoff | lockdep\n");
        break;
      }
      if (!printed && dt >= 50) {
        uart_puts("[deadlock-lab] waiting for deadlock...\n");
        printed = true;
      }
      if (dt >= 200) {
        uart_puts("[deadlock-lab] FAIL: deadlock not observed (timing?)\n");
        break;
      }
    } else if (g_dl_mode == 3u || g_dl_mode == 4u) {
      if (g_dl_done >= 2) {
        uart_puts("[deadlock-lab] result PASS\n");
        break;
      }
      if (dt >= 200) {
        uart_puts("[deadlock-lab] FAIL: timeout waiting for completion\n");
        break;
      }
    } else {
      // Mode 5 halts inside lockdep on success; nothing to do here.
      if (dt >= 200) {
        uart_puts("[deadlock-lab] FAIL: lockdep did not trigger\n");
        break;
      }
    }

    spin(20000);
    thread_yield();
  }

  while (1) {
    asm volatile("wfe");
  }
}
}  // namespace

extern "C" void sync_lab_setup(unsigned mode) {
  if (mode == 1u) {
    uart_puts("[sync-lab] setup\n");
    g_pi_enabled = 0;
    g_high_done = 0;

    mutex_init(&g_lock);
    mutex_set_pi_enabled(&g_lock, 0);
    sem_init(&g_start, 0);
    sem_init(&g_hold_high, 0);

    // Classic 3-thread priority inversion:
    // - L (low): holds mutex
    // - H (high): blocks on mutex
    // - M (medium): runs, starving L until PI boosts L
    Thread* l = thread_create_prio(low_thread, nullptr, 16 * 1024, /*prio=*/5);
    Thread* m = thread_create_prio(medium_thread, nullptr, 16 * 1024, /*prio=*/10);
    Thread* h = thread_create_prio(high_thread, nullptr, 16 * 1024, /*prio=*/20);

    if (!l || !m || !h) {
      uart_puts("[sync-lab] thread_create failed\n");
      while (1) {
        asm volatile("wfe");
      }
    }

    sched_add(l);
    sched_add(m);
    sched_add(h);
    return;
  }

  if (mode >= 2u && mode <= 5u) {
    uart_puts("[deadlock-lab] setup\n");
    g_dl_mode = mode;
    g_dl_done = 0;
    g_dl_ready = 0;
    g_dl_t1 = nullptr;
    g_dl_t2 = nullptr;

    mutex_init(&g_dl_a);
    mutex_init(&g_dl_b);
    sem_init(&g_dl_hold, 0);

    // Two workers intentionally acquire locks in opposite order. A watchdog
    // thread guarantees there is always at least one runnable thread.
    g_dl_t1 = thread_create_prio(deadlock_worker, reinterpret_cast<void*>(1), 16 * 1024, /*prio=*/20);
    g_dl_t2 = thread_create_prio(deadlock_worker, reinterpret_cast<void*>(2), 16 * 1024, /*prio=*/20);
    Thread* w = thread_create_prio(deadlock_watchdog, nullptr, 16 * 1024, /*prio=*/5);

    if (!g_dl_t1 || !g_dl_t2 || !w) {
      uart_puts("[deadlock-lab] thread_create failed\n");
      while (1) {
        asm volatile("wfe");
      }
    }

    sched_add(g_dl_t1);
    sched_add(g_dl_t2);
    sched_add(w);
    return;
  }

  uart_puts("[sync-lab] unknown mode\n");
  uart_puts("[sync-lab] modes: 1=pi, 2=deadlock, 3=ordering, 4=trylock, 5=lockdep\n");
  while (1) {
    asm volatile("wfe");
  }
}
