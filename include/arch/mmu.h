#pragma once
#include <stdint.h>

// Initialize the MMU/cache scaffolding. When |enable| is false, the routine
// leaves the MMU disabled but allows early bring-up code to prepare for a
// later enable step.
void mmu_init(bool enable);

// Dump key EL1 system registers to the UART for diagnostics.
void mmu_dump_state();
