#pragma once
#include <stdint.h>

// Initialize the MMU/cache scaffolding. When |enable| is false, the routine
// leaves the MMU disabled but allows early bring-up code to prepare for a
// later enable step.
void mmu_init(bool enable);

// Dump key EL1 system registers to the UART for diagnostics.
void mmu_dump_state();

// Return 1 if EL1 stage-1 translation is enabled.
int mmu_enabled(void);

// Mark the 4 KiB page containing |addr| as invalid (guard page).
// Returns 0 on success, -1 on unsupported address/state.
int mmu_guard_page(void* addr);
