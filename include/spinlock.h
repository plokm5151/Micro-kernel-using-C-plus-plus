#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A minimal AArch64 spinlock implementation.
//
// - raw_spinlock: mutual exclusion + acquire/release ordering only.
// - spinlock:     raw_spinlock + preemption control while held.
//
// This project currently runs single-core, but the ordering semantics are
// written to be correct under weak AArch64 memory ordering.
struct raw_spinlock {
  volatile uint32_t v;  // 0=unlocked, 1=locked
};

struct spinlock {
  struct raw_spinlock raw;
};

void raw_spin_init(struct raw_spinlock* l);
void raw_spin_lock(struct raw_spinlock* l);
int  raw_spin_trylock(struct raw_spinlock* l);  // 0 on success, -1 on failure
void raw_spin_unlock(struct raw_spinlock* l);

void spin_init(struct spinlock* l);
void spin_lock(struct spinlock* l);
int  spin_trylock(struct spinlock* l);          // 0 on success, -1 on failure
void spin_unlock(struct spinlock* l);

// spin_lock + disable local IRQs (typical "used in both thread/IRQ contexts").
unsigned long spin_lock_irqsave(struct spinlock* l);
void spin_unlock_irqrestore(struct spinlock* l, unsigned long flags);

#ifdef __cplusplus
}
#endif

