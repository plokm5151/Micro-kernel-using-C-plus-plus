#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Lock lab modes (deterministic, single-core):
// - mode=1: demonstrate raw spin deadlock under strict priority preemption
// - mode=2: demonstrate preempt-safe spin_lock() fix (expected PASS)
// - mode=3: demonstrate IRQ reentrancy deadlock without irqsave (expected hang)
// - mode=4: demonstrate spin_lock_irqsave() fix (expected PASS)
void lock_lab_setup(unsigned mode);

// Called from the timer IRQ path when LOCK_LAB_MODE != 0.
void lock_lab_irq_tick(void);

#ifdef __cplusplus
}
#endif

