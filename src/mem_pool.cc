#include "mem_pool.h"

#include <stdint.h>

namespace {
#ifndef MEM_POOL_DEBUG
#define MEM_POOL_DEBUG 0
#endif

static inline uintptr_t align_up(uintptr_t v, uintptr_t a) {
  return (v + a - 1u) & ~(a - 1u);
}

static inline bool is_pow2(uintptr_t v) {
  return v && ((v & (v - 1u)) == 0u);
}

static inline size_t clamp_align(size_t a) {
  if (a < alignof(void*)) return alignof(void*);
  if (!is_pow2(a)) {
    size_t x = 1u;
    while (x < a) x <<= 1u;
    return x;
  }
  return a;
}

static inline void* load_next(void* p) {
  return *reinterpret_cast<void**>(p);
}

static inline void store_next(void* p, void* next) {
  *reinterpret_cast<void**>(p) = next;
}

#if MEM_POOL_DEBUG
static bool freelist_contains(void* head, const void* p) {
  for (void* it = head; it; it = load_next(it)) {
    if (it == p) return true;
  }
  return false;
}
#endif
}  // namespace

extern "C" int mem_pool_init(struct mem_pool* pool, void* backing, size_t backing_size, size_t block_size) {
  if (!pool || !backing || backing_size == 0 || block_size == 0) {
    return -1;
  }
  if (block_size < sizeof(void*)) {
    return -1;
  }

  const size_t align = clamp_align(16);
  const uintptr_t raw = reinterpret_cast<uintptr_t>(backing);
  const uintptr_t aligned = align_up(raw, align);
  if (aligned < raw) return -1;
  const size_t lost = static_cast<size_t>(aligned - raw);
  if (lost >= backing_size) return -1;
  size_t usable = backing_size - lost;

  const size_t blk = static_cast<size_t>(align_up(block_size, align));
  if (blk == 0) return -1;
  const size_t count = usable / blk;
  if (count == 0) return -1;

  pool->base = reinterpret_cast<uint8_t*>(aligned);
  pool->block_size = blk;
  pool->block_count = count;
  pool->free_count = count;

  void* head = nullptr;
  for (size_t i = 0; i < count; ++i) {
    void* b = pool->base + i * blk;
    store_next(b, head);
    head = b;
  }
  pool->free_list = head;
  return 0;
}

extern "C" void* mem_pool_alloc(struct mem_pool* pool) {
  if (!pool || !pool->free_list) return nullptr;
  void* p = pool->free_list;
  pool->free_list = load_next(p);
  if (pool->free_count) pool->free_count--;
  store_next(p, nullptr);
  return p;
}

extern "C" int mem_pool_free(struct mem_pool* pool, void* p) {
  if (!pool || !p) return -1;
  if (!mem_pool_owns(pool, p)) return -1;

#if MEM_POOL_DEBUG
  if (freelist_contains(pool->free_list, p)) {
    return -1;
  }
#endif

  store_next(p, pool->free_list);
  pool->free_list = p;
  pool->free_count++;
  return 0;
}

extern "C" int mem_pool_owns(const struct mem_pool* pool, const void* p) {
  if (!pool || !pool->base || !p) return 0;
  const uintptr_t start = reinterpret_cast<uintptr_t>(pool->base);
  const uintptr_t end = start + pool->block_count * pool->block_size;
  const uintptr_t v = reinterpret_cast<uintptr_t>(p);
  if (v < start || v >= end) return 0;
  const uintptr_t off = v - start;
  return (off % pool->block_size) == 0u;
}

extern "C" size_t mem_pool_block_size(const struct mem_pool* pool) {
  return pool ? pool->block_size : 0;
}

extern "C" size_t mem_pool_capacity(const struct mem_pool* pool) {
  return pool ? pool->block_count : 0;
}

extern "C" size_t mem_pool_available(const struct mem_pool* pool) {
  return pool ? pool->free_count : 0;
}

