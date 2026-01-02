#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dma_cb_t)(void* user, int status);

int dma_submit_memcpy(void* dst, const void* src, size_t len,
                      dma_cb_t cb, void* user);
void dma_poll_complete(void);

// Optional helper: allocate from the dedicated DMA window.
void* dma_alloc_buffer(size_t len, size_t align);

// Linker-defined DMA window range (see boot/kernel.ld).
extern char __dma_nc_start[];
extern char __dma_nc_end[];

#ifdef __cplusplus
}
#endif

#if defined(DMA_WINDOW_POLICY_NONCACHEABLE) && defined(DMA_WINDOW_POLICY_CACHEABLE)
#error "Define only one of DMA_WINDOW_POLICY_{CACHEABLE,NONCACHEABLE}"
#endif
#if !defined(DMA_WINDOW_POLICY_NONCACHEABLE) && !defined(DMA_WINDOW_POLICY_CACHEABLE)
#define DMA_WINDOW_POLICY_CACHEABLE 1
#endif

#if defined(DMA_WINDOW_POLICY_NONCACHEABLE)
#define DMA_WINDOW_POLICY_STR "NONCACHEABLE"
#else
#define DMA_WINDOW_POLICY_STR "CACHEABLE"
#endif

// Non-cacheable alias mapping:
//   cacheable VA:      0x40000000..0x7FFFFFFF  (Normal WBWA)
//   non-cacheable VA:  0x80000000..0xBFFFFFFF  (Normal NC)
// Conversion is defined only for addresses within the DMA window.
#define DMA_NOCACHE_ALIAS_OFFSET 0x40000000ull

static inline uintptr_t dma_window_start(void) {
  return (uintptr_t)__dma_nc_start;
}

static inline uintptr_t dma_window_end(void) {
  return (uintptr_t)__dma_nc_end;
}

static inline int dma_window_contains_range(const void* p, size_t len) {
  if (!p || len == 0) return 0;
  uintptr_t start = (uintptr_t)p;
  uintptr_t end = start + (len - 1u);
  if (end < start) return 0;
  return start >= dma_window_start() && end < dma_window_end();
}

static inline void* dma_window_to_nocache_alias(const void* p, size_t len) {
  if (!dma_window_contains_range(p, len)) return (void*)0;
  return (void*)((uintptr_t)p + (uintptr_t)DMA_NOCACHE_ALIAS_OFFSET);
}

static inline void* dma_window_from_nocache_alias(const void* p, size_t len) {
  if (!p || len == 0) return (void*)0;
  uintptr_t addr = (uintptr_t)p;
  uintptr_t base = addr - (uintptr_t)DMA_NOCACHE_ALIAS_OFFSET;
  if (base > addr) return (void*)0;
  return dma_window_contains_range((const void*)base, len) ? (void*)base : (void*)0;
}
