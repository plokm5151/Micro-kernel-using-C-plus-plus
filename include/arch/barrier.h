#pragma once

#include <stddef.h>

// Base memory barriers (Inner Shareable domain).
static inline void dmb_ish(void) {
  asm volatile("dmb ish" ::: "memory");
}

static inline void dsb_ish(void) {
  asm volatile("dsb ish" ::: "memory");
}

// Base memory barriers (Outer Shareable domain).
static inline void dmb_osh(void) {
  asm volatile("dmb osh" ::: "memory");
}

static inline void dmb_oshst(void) {
  asm volatile("dmb oshst" ::: "memory");
}

static inline void dmb_oshld(void) {
  asm volatile("dmb oshld" ::: "memory");
}

static inline void dsb_osh(void) {
  asm volatile("dsb osh" ::: "memory");
}

static inline void isb(void) {
  asm volatile("isb" ::: "memory");
}

// DMA-friendly ordering helpers (mirroring common Linux semantics).
static inline void dma_wmb(void) {
  dmb_oshst();
}

static inline void dma_rmb(void) {
  dmb_oshld();
}

static inline void dma_mb(void) {
  dmb_osh();
}

// Cache maintenance over virtual address ranges (Normal cacheable memory only).
// When the MMU maps a region as Device or Non-cacheable, callers must not invoke
// these helpers on that region.
#ifdef __cplusplus
extern "C" {
#endif

int dcache_enabled(void);
void dc_cvac_range(const void* p, size_t len);
void dc_civac_range(const void* p, size_t len);
void dc_ivac_range(const void* p, size_t len);

#ifdef __cplusplus
}
#endif
