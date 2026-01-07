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

volatile int g_pi_enabled = 0;
volatile int g_high_done = 0;

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

  while (1) {
    asm volatile("wfe");
  }
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
}  // namespace

extern "C" void sync_lab_setup(unsigned mode) {
  (void)mode;

  uart_puts("[sync-lab] setup\n");
  g_pi_enabled = 0;
  g_high_done = 0;

  mutex_init(&g_lock);
  mutex_set_pi_enabled(&g_lock, 0);
  sem_init(&g_start, 0);

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
}

