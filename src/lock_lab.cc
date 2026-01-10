#include "lock_lab.h"

#include <stdint.h>

#include "arch/cpu_local.h"
#include "drivers/uart_pl011.h"
#include "spinlock.h"
#include "sync.h"
#include "thread.h"

namespace {
constexpr uint64_t kDeadlockDetectTicks = 40;  // @1kHz, ~40ms

volatile unsigned g_mode = 0;

// Mode 1/2 state (preemption vs raw spin deadlock).
spinlock g_preempt_lock;
semaphore g_preempt_start;
volatile int g_low_locked = 0;
volatile int g_low_unlocked = 0;
volatile int g_high_started = 0;
volatile int g_high_acquired = 0;
volatile uint64_t g_high_start_tick = 0;
volatile int g_deadlock_reported = 0;

// Mode 3/4 state (IRQ reentrancy vs irqsave).
spinlock g_irq_lock;
volatile int g_irq_worker_holding = 0;
volatile int g_irq_isr_acquired = 0;
volatile int g_irq_deadlock_reported = 0;

static inline void spin(unsigned n) {
  for (volatile unsigned i = 0; i < n; ++i) {
    asm volatile("" ::: "memory");
  }
}

static void preempt_low_thread(void*) {
  uart_puts("[lock-lab] L start\n");

  g_low_locked = 0;
  g_low_unlocked = 0;

  if (g_mode == 1u) {
    raw_spin_lock(&g_preempt_lock.raw);
  } else {
    spin_lock(&g_preempt_lock);
  }
  g_low_locked = 1;
  uart_puts("[lock-lab] L locked\n");

  // Release H while still holding the lock.
  sem_up(&g_preempt_start);

  // Hold the lock long enough for timer-driven preemption pressure, but keep
  // it bounded and time-based (ticks) so it is deterministic across QEMU speeds.
  auto* cpu = cpu_local();
  const uint64_t t0 = cpu ? cpu->ticks : 0;
  while (cpu && (cpu->ticks - t0) < 10u) {
    spin(20000);
  }

  uart_puts("[lock-lab] L unlocking\n");
  if (g_mode == 1u) {
    raw_spin_unlock(&g_preempt_lock.raw);
  } else {
    spin_unlock(&g_preempt_lock);
  }
  g_low_unlocked = 1;
  uart_puts("[lock-lab] L unlocked\n");

  // Hand off to H deterministically.
  thread_yield();

  while (1) {
    asm volatile("wfe");
  }
}

static void preempt_high_thread(void*) {
  uart_puts("[lock-lab] H start\n");
  sem_down(&g_preempt_start);

  uart_puts("[lock-lab] H lock...\n");
  g_high_started = 1;
  g_high_start_tick = cpu_local() ? cpu_local()->ticks : 0;

  if (g_mode == 1u) {
    raw_spin_lock(&g_preempt_lock.raw);
  } else {
    spin_lock(&g_preempt_lock);
  }
  g_high_acquired = 1;
  uart_puts("[lock-lab] H acquired\n");

  if (g_mode == 1u) {
    raw_spin_unlock(&g_preempt_lock.raw);
  } else {
    spin_unlock(&g_preempt_lock);
  }

  if (g_mode == 2u) {
    uart_puts("[lock-lab] result PASS\n");
  } else {
    uart_puts("[lock-lab] BUG: raw spinlock unexpectedly made progress\n");
  }

  while (1) {
    asm volatile("wfe");
  }
}

static void irq_worker_thread(void*) {
  uart_puts("[lock-lab] irq-worker start\n");

  if (g_mode == 3u) {
    // Bug: take a lock that will also be taken in the timer IRQ without irqsave.
    spin_lock(&g_irq_lock);
    g_irq_worker_holding = 1;
    uart_puts("[lock-lab] irq-worker locked (no irqsave)\n");

    // Hold long enough for the timer IRQ to run and demonstrate the deadlock.
    while (1) {
      spin(200000);
    }
  }

  if (g_mode == 4u) {
    unsigned long flags = spin_lock_irqsave(&g_irq_lock);
    g_irq_worker_holding = 1;
    uart_puts("[lock-lab] irq-worker locked (irqsave)\n");

    // Keep the critical section short; interrupts are disabled here.
    spin(50000);
    spin_unlock_irqrestore(&g_irq_lock, flags);
    g_irq_worker_holding = 0;
    uart_puts("[lock-lab] irq-worker unlocked (irqsave)\n");

    // Wait for the simulated ISR path to take the lock at least once.
    while (!g_irq_isr_acquired) {
      asm volatile("wfe");
    }
    uart_puts("[lock-lab] irqsave result PASS\n");

    while (1) {
      asm volatile("wfe");
    }
  }

  uart_puts("[lock-lab] unknown mode\n");
  while (1) {
    asm volatile("wfe");
  }
}
}  // namespace

extern "C" void lock_lab_setup(unsigned mode) {
  g_mode = mode;

  uart_puts("[lock-lab] setup\n");
  uart_puts("[lock-lab] mode=");
  uart_print_u64(static_cast<unsigned long long>(mode));
  uart_puts("\n");

  spin_init(&g_preempt_lock);
  sem_init(&g_preempt_start, 0);
  g_low_locked = 0;
  g_low_unlocked = 0;
  g_high_started = 0;
  g_high_acquired = 0;
  g_high_start_tick = 0;
  g_deadlock_reported = 0;

  spin_init(&g_irq_lock);
  g_irq_worker_holding = 0;
  g_irq_isr_acquired = 0;
  g_irq_deadlock_reported = 0;

  if (mode == 1u || mode == 2u) {
    // L runs first (H blocked on semaphore), then we release H while L still
    // holds the lock to create a deterministic preemption window.
    Thread* l = thread_create_prio(preempt_low_thread, nullptr, 16 * 1024, /*prio=*/5);
    Thread* h = thread_create_prio(preempt_high_thread, nullptr, 16 * 1024, /*prio=*/20);
    if (!l || !h) {
      uart_puts("[lock-lab] thread_create failed\n");
      while (1) {
        asm volatile("wfe");
      }
    }
    sched_add(l);
    sched_add(h);
    return;
  }

  if (mode == 3u || mode == 4u) {
    Thread* w = thread_create_prio(irq_worker_thread, nullptr, 16 * 1024, /*prio=*/10);
    if (!w) {
      uart_puts("[lock-lab] thread_create failed\n");
      while (1) {
        asm volatile("wfe");
      }
    }
    sched_add(w);
    return;
  }

  uart_puts("[lock-lab] unknown mode\n");
  uart_puts("[lock-lab] modes: 1=raw-preempt-deadlock, 2=spin-preempt-safe, 3=irq-deadlock, 4=irqsave\n");
  while (1) {
    asm volatile("wfe");
  }
}

extern "C" void lock_lab_irq_tick(void) {
  if (g_mode == 1u) {
    if (g_deadlock_reported) return;
    if (!g_low_locked || g_low_unlocked) return;
    if (!g_high_started || g_high_acquired) return;

    const uint64_t now = cpu_local() ? cpu_local()->ticks : 0;
    uint64_t start = g_high_start_tick;
    if (start == 0) {
      g_high_start_tick = now;
      start = now;
    }
    if (now - start >= kDeadlockDetectTicks) {
      g_deadlock_reported = 1;
      uart_puts("[lock-lab] raw deadlock observed\n");
    }
    return;
  }

  if (g_mode == 3u) {
    if (g_irq_deadlock_reported) return;
    if (!g_irq_worker_holding) return;
    g_irq_deadlock_reported = 1;
    uart_puts("[lock-lab] irq reentrancy deadlock observed (missing irqsave)\n");

    // Demonstrate the bug: the timer IRQ tries to acquire a lock held by the
    // interrupted thread. This cannot make progress and will hang the system.
    raw_spin_lock(&g_irq_lock.raw);
    return;
  }

  if (g_mode == 4u) {
    if (g_irq_isr_acquired) return;
    if (g_irq_worker_holding) return;  // should be impossible with irqsave, but be safe.
    raw_spin_lock(&g_irq_lock.raw);
    raw_spin_unlock(&g_irq_lock.raw);
    g_irq_isr_acquired = 1;
    return;
  }
}
