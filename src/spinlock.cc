#include "spinlock.h"

#include "arch/irqflags.h"
#include "preempt.h"

namespace {
static inline void cpu_relax(void) {
  asm volatile("yield" ::: "memory");
}
}  // namespace

extern "C" void raw_spin_init(struct raw_spinlock* l) {
  if (!l) return;
  l->v = 0;
}

extern "C" int raw_spin_trylock(struct raw_spinlock* l) {
  if (!l) return -1;

  uint32_t out = 0;
  uint32_t status = 0;
  asm volatile(
      // Read lock with acquire semantics.
      "ldaxr %w0, [%2]\n"
      "cbnz  %w0, 1f\n"
      // Attempt to set it to 1.
      "mov   %w0, #1\n"
      "stxr  %w1, %w0, [%2]\n"
      "cbnz  %w1, 1f\n"
      // Success => return 0.
      "mov   %w0, #0\n"
      "b     2f\n"
      "1:\n"
      "clrex\n"
      // Failure => return -1.
      "mov   %w0, #-1\n"
      "2:\n"
      : "=&r"(out), "=&r"(status)
      : "r"(&l->v)
      : "memory");
  return static_cast<int>(out);
}

extern "C" void raw_spin_lock(struct raw_spinlock* l) {
  if (!l) return;
  while (raw_spin_trylock(l) != 0) {
    while (l->v != 0) {
      cpu_relax();
    }
    cpu_relax();
  }
}

extern "C" void raw_spin_unlock(struct raw_spinlock* l) {
  if (!l) return;
  // Store-release: all prior memory ops happen-before the unlock becomes visible.
  asm volatile("stlr wzr, [%0]" : : "r"(&l->v) : "memory");
}

extern "C" void spin_init(struct spinlock* l) {
  if (!l) return;
  raw_spin_init(&l->raw);
}

extern "C" void spin_lock(struct spinlock* l) {
  if (!l) return;
  for (;;) {
    // Disable preemption only around the actual acquisition to avoid a window
    // where the lock is held but the owner is still preemptible. Keep waiting
    // preemptible so the lock holder can run on a single core.
    preempt_disable();
    if (raw_spin_trylock(&l->raw) == 0) {
      return;
    }
    preempt_enable();

    while (l->raw.v != 0) {
      cpu_relax();
    }
    cpu_relax();
  }
}

extern "C" int spin_trylock(struct spinlock* l) {
  if (!l) return -1;
  preempt_disable();
  if (raw_spin_trylock(&l->raw) == 0) {
    return 0;
  }
  preempt_enable();
  return -1;
}

extern "C" void spin_unlock(struct spinlock* l) {
  if (!l) return;
  raw_spin_unlock(&l->raw);
  preempt_enable();
}

extern "C" unsigned long spin_lock_irqsave(struct spinlock* l) {
  if (!l) return 0;
  unsigned long flags = local_irq_save();
  raw_spin_lock(&l->raw);
  preempt_disable();
  return flags;
}

extern "C" void spin_unlock_irqrestore(struct spinlock* l, unsigned long flags) {
  if (!l) return;
  raw_spin_unlock(&l->raw);
  local_irq_restore(flags);
  preempt_enable();
}
