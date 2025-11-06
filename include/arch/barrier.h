#pragma once

#include <stddef.h>

// Base memory barriers (Inner Shareable domain).
static inline void dmb_ish(void) {
  asm volatile("dmb ish" ::: "memory");
}

static inline void dsb_ish(void) {
  asm volatile("dsb ish" ::: "memory");
}

static inline void isb(void) {
  asm volatile("isb" ::: "memory");
}

// DMA-friendly ordering helpers (mirroring common Linux semantics).
static inline void dma_wmb(void) {
  asm volatile("dmb ishst" ::: "memory");
}

static inline void dma_rmb(void) {
  asm volatile("dmb ishld" ::: "memory");
}

static inline void dma_mb(void) {
  asm volatile("dmb ish" ::: "memory");
}

// Cache maintenance over virtual address ranges (Normal cacheable memory only).
// When the MMU maps a region as Device or Non-cacheable, callers must not invoke
// these helpers on that region.
#ifdef __cplusplus
extern "C" {
#endif

void dc_cvac_range(const void* p, size_t len);
void dc_civac_range(const void* p, size_t len);
void dc_ivac_range(const void* p, size_t len);

#ifdef __cplusplus
}
#endif

