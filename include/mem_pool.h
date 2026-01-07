#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Fixed-size memory pool backed by a caller-provided region.
//
// Goals (embedded-friendly):
// - O(1) allocation/free (single pointer pop/push)
// - no external fragmentation
// - predictable behavior under interrupt/preemption when guarded externally
//
// This pool is intentionally simple: it does not do per-block metadata beyond
// storing the next pointer inside freed blocks.
struct mem_pool {
  uint8_t* base;
  size_t   block_size;
  size_t   block_count;
  void*    free_list;   // singly-linked list (pointer stored in freed blocks)
  size_t   free_count;
};

// Initialize a pool over [backing, backing+backing_size).
// Returns 0 on success, -1 on invalid parameters.
int mem_pool_init(struct mem_pool* pool, void* backing, size_t backing_size, size_t block_size);

// Allocate one block; returns nullptr if exhausted.
void* mem_pool_alloc(struct mem_pool* pool);

// Free a previously allocated block. Returns 0 on success, -1 on invalid pointer.
int mem_pool_free(struct mem_pool* pool, void* p);

// Pointer classification helpers.
int mem_pool_owns(const struct mem_pool* pool, const void* p);

// Stats.
size_t mem_pool_block_size(const struct mem_pool* pool);
size_t mem_pool_capacity(const struct mem_pool* pool);
size_t mem_pool_available(const struct mem_pool* pool);

#ifdef __cplusplus
}
#endif

