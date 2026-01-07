#include "mem_lab.h"

#include <stddef.h>
#include <stdint.h>

#include "drivers/uart_pl011.h"
#include "kmem.h"
#include "mem_pool.h"

namespace {
static void put_u64(const char* label, uint64_t v) {
  uart_puts(label);
  uart_print_u64(static_cast<unsigned long long>(v));
  uart_puts("\n");
}

static inline size_t align_up(size_t v, size_t a) {
  return (v + a - 1u) & ~(a - 1u);
}

// ---------- Demo 1: fixed-size pool internal fragmentation ----------
struct pool_alloc_rec {
  void*  p;
  size_t req;
};

static uint32_t lcg_next(uint32_t* state) {
  *state = (*state * 1664525u) + 1013904223u;
  return *state;
}

static void demo_pool_internal_fragmentation() {
  uart_puts("[mem-lab][pool] internal fragmentation demo\n");

  constexpr size_t kBackingBytes = 64 * 1024;
  constexpr size_t kBlockSize = 256;
  constexpr size_t kMaxAllocs = 128;

  void* backing = kmem_alloc_aligned(kBackingBytes, 64);
  if (!backing) {
    uart_puts("[mem-lab][pool] backing alloc failed\n");
    return;
  }

  mem_pool pool;
  if (mem_pool_init(&pool, backing, kBackingBytes, kBlockSize) != 0) {
    uart_puts("[mem-lab][pool] mem_pool_init failed\n");
    return;
  }

  pool_alloc_rec recs[kMaxAllocs] = {};
  uint32_t rng = 0x12345678u;

  size_t allocs = 0;
  size_t wasted = 0;
  size_t provided = 0;

  for (size_t i = 0; i < kMaxAllocs; ++i) {
    const size_t req = 1u + (lcg_next(&rng) % kBlockSize);
    void* p = mem_pool_alloc(&pool);
    if (!p) break;
    recs[i] = {p, req};
    allocs++;
    provided += mem_pool_block_size(&pool);
    wasted += (mem_pool_block_size(&pool) - req);
  }

  put_u64("[mem-lab][pool] allocs=", allocs);
  put_u64("[mem-lab][pool] block_size=", mem_pool_block_size(&pool));
  put_u64("[mem-lab][pool] provided_bytes=", provided);
  put_u64("[mem-lab][pool] requested_bytes=", (provided - wasted));
  put_u64("[mem-lab][pool] wasted_bytes=", wasted);
  if (provided) {
    const uint64_t pct = (static_cast<uint64_t>(wasted) * 100u) / static_cast<uint64_t>(provided);
    put_u64("[mem-lab][pool] internal_frag_pct=", pct);
  }

  // Free everything (should be O(1) per op).
  for (size_t i = 0; i < allocs; ++i) {
    if (recs[i].p) {
      mem_pool_free(&pool, recs[i].p);
    }
  }
  put_u64("[mem-lab][pool] free_count=", mem_pool_available(&pool));
}

// ---------- Demo 2: "malloc" external fragmentation + variable cost ----------
//
// This is a deliberately tiny first-fit allocator used only for demonstration.
// It is not wired into the kernel allocator path.
struct heap_block {
  uint32_t magic;
  uint32_t flags;  // bit0 = free
  size_t   size;   // payload bytes
};

constexpr uint32_t kHeapMagic = 0x6d656d31u;  // "mem1"
constexpr uint32_t kHeapFree = 1u;

static heap_block* heap_next(heap_block* b) {
  uint8_t* p = reinterpret_cast<uint8_t*>(b);
  p += sizeof(heap_block);
  p += b->size;
  p = reinterpret_cast<uint8_t*>(align_up(reinterpret_cast<size_t>(p), 16));
  return reinterpret_cast<heap_block*>(p);
}

static void heap_init(void* mem, size_t bytes) {
  auto* b = reinterpret_cast<heap_block*>(mem);
  b->magic = kHeapMagic;
  b->flags = kHeapFree;
  b->size = bytes - sizeof(heap_block);
}

static void* heap_alloc_first_fit(void* heap, size_t heap_bytes, size_t req, size_t* out_steps) {
  if (!heap || req == 0) return nullptr;
  req = align_up(req, 16);

  uint8_t* base = reinterpret_cast<uint8_t*>(heap);
  uint8_t* end = base + heap_bytes;
  size_t steps = 0;

  for (heap_block* b = reinterpret_cast<heap_block*>(base);
       reinterpret_cast<uint8_t*>(b) + sizeof(heap_block) <= end;
       b = heap_next(b)) {
    if (b->magic != kHeapMagic) break;
    steps++;
    if ((b->flags & kHeapFree) == 0) continue;
    if (b->size < req) continue;

    // Split if there is room for a new header and a small payload.
    uint8_t* payload = reinterpret_cast<uint8_t*>(b) + sizeof(heap_block);
    uint8_t* old_end = reinterpret_cast<uint8_t*>(align_up(reinterpret_cast<size_t>(payload + b->size), 16));
    uint8_t* new_hdr = reinterpret_cast<uint8_t*>(align_up(reinterpret_cast<size_t>(payload + req), 16));
    uint8_t* new_payload = new_hdr + sizeof(heap_block);
    if (new_payload + 16u <= old_end) {
      auto* nb = reinterpret_cast<heap_block*>(new_hdr);
      nb->magic = kHeapMagic;
      nb->flags = kHeapFree;
      nb->size = static_cast<size_t>(old_end - new_payload);
      b->size = req;
    }

    b->flags &= ~kHeapFree;
    if (out_steps) *out_steps = steps;
    return reinterpret_cast<uint8_t*>(b) + sizeof(heap_block);
  }

  if (out_steps) *out_steps = steps;
  return nullptr;
}

static void heap_coalesce(void* heap, size_t heap_bytes) {
  uint8_t* base = reinterpret_cast<uint8_t*>(heap);
  uint8_t* end = base + heap_bytes;

  for (heap_block* b = reinterpret_cast<heap_block*>(base);
       reinterpret_cast<uint8_t*>(b) + sizeof(heap_block) <= end;) {
    if (b->magic != kHeapMagic) break;
    heap_block* n = heap_next(b);
    if (reinterpret_cast<uint8_t*>(n) + sizeof(heap_block) > end) break;
    if (n->magic != kHeapMagic) break;

    if ((b->flags & kHeapFree) && (n->flags & kHeapFree)) {
      // Merge n into b.
      uint8_t* b_payload = reinterpret_cast<uint8_t*>(b) + sizeof(heap_block);
      uint8_t* n_payload = reinterpret_cast<uint8_t*>(n) + sizeof(heap_block);
      const size_t gap = static_cast<size_t>(n_payload - (b_payload + b->size));
      b->size = b->size + gap + n->size;
      continue;
    }
    b = n;
  }
}

static void heap_free(void* heap, size_t heap_bytes, void* p) {
  if (!heap || !p) return;
  uint8_t* base = reinterpret_cast<uint8_t*>(heap);
  uint8_t* end = base + heap_bytes;
  uint8_t* v = reinterpret_cast<uint8_t*>(p);
  if (v < base + sizeof(heap_block) || v >= end) return;

  auto* b = reinterpret_cast<heap_block*>(v - sizeof(heap_block));
  if (b->magic != kHeapMagic) return;
  b->flags |= kHeapFree;
  heap_coalesce(heap, heap_bytes);
}

static void heap_stats(void* heap, size_t heap_bytes, size_t* total_free, size_t* largest_free, size_t* free_blocks) {
  if (total_free) *total_free = 0;
  if (largest_free) *largest_free = 0;
  if (free_blocks) *free_blocks = 0;

  uint8_t* base = reinterpret_cast<uint8_t*>(heap);
  uint8_t* end = base + heap_bytes;
  for (heap_block* b = reinterpret_cast<heap_block*>(base);
       reinterpret_cast<uint8_t*>(b) + sizeof(heap_block) <= end;
       b = heap_next(b)) {
    if (b->magic != kHeapMagic) break;
    if ((b->flags & kHeapFree) == 0) continue;
    if (total_free) *total_free += b->size;
    if (largest_free && b->size > *largest_free) *largest_free = b->size;
    if (free_blocks) (*free_blocks)++;
  }
}

static void demo_malloc_external_fragmentation() {
  uart_puts("[mem-lab][malloc] external fragmentation demo\n");

  constexpr size_t kHeapBytes = 64 * 1024;
  void* heap = kmem_alloc_aligned(kHeapBytes, 16);
  if (!heap) {
    uart_puts("[mem-lab][malloc] heap alloc failed\n");
    return;
  }

  heap_init(heap, kHeapBytes);
  put_u64("[mem-lab][malloc] heap_bytes=", kHeapBytes);
  put_u64("[mem-lab][malloc] header_bytes=", sizeof(heap_block));

  // Allocate many medium chunks, then free every other chunk.
  constexpr size_t kChunk = 1024;
  constexpr size_t kMax = 32;
  void* ptrs[kMax] = {};

  size_t steps_total = 0;
  size_t steps_max = 0;
  size_t allocs = 0;

  for (size_t i = 0; i < kMax; ++i) {
    size_t steps = 0;
    void* p = heap_alloc_first_fit(heap, kHeapBytes, kChunk, &steps);
    if (!p) break;
    ptrs[i] = p;
    allocs++;
    steps_total += steps;
    if (steps > steps_max) steps_max = steps;
  }
  put_u64("[mem-lab][malloc] allocs=", allocs);
  put_u64("[mem-lab][malloc] alloc_steps_avg=", allocs ? (steps_total / allocs) : 0);
  put_u64("[mem-lab][malloc] alloc_steps_max=", steps_max);

  for (size_t i = 0; i < allocs; i += 2) {
    heap_free(heap, kHeapBytes, ptrs[i]);
    ptrs[i] = nullptr;
  }

  size_t total_free = 0, largest_free = 0, free_blocks = 0;
  heap_stats(heap, kHeapBytes, &total_free, &largest_free, &free_blocks);

  put_u64("[mem-lab][malloc] free_blocks=", free_blocks);
  put_u64("[mem-lab][malloc] total_free=", total_free);
  put_u64("[mem-lab][malloc] largest_free=", largest_free);
  if (total_free) {
    const uint64_t frag_pct = (total_free > largest_free)
                                  ? ((static_cast<uint64_t>(total_free - largest_free) * 100u) / static_cast<uint64_t>(total_free))
                                  : 0u;
    put_u64("[mem-lab][malloc] external_frag_pct=", frag_pct);
  }

  // Demonstrate a classic external fragmentation failure: enough total free,
  // but no sufficiently large contiguous chunk.
  constexpr size_t kBig = 8 * 1024;
  if (total_free > kBig && largest_free < kBig) {
    uart_puts("[mem-lab][malloc] expected: total_free > big && largest_free < big\n");
  }
  size_t steps = 0;
  void* big = heap_alloc_first_fit(heap, kHeapBytes, kBig, &steps);
  if (!big) {
    uart_puts("[mem-lab][malloc] big alloc FAILED (fragmentation)\n");
  } else {
    uart_puts("[mem-lab][malloc] big alloc unexpectedly succeeded\n");
  }
  put_u64("[mem-lab][malloc] big_alloc_steps=", steps);
}

static void print_embedded_malloc_takeaways() {
  uart_puts("[mem-lab] why embedded often avoids malloc/free:\n");
  uart_puts("  - unpredictable latency (first-fit search/coalesce steps vary)\n");
  uart_puts("  - external fragmentation (alloc can fail with memory still free)\n");
  uart_puts("  - metadata + alignment overhead (headers consume RAM)\n");
  uart_puts("  - harder to reason about worst-case behavior under load\n");
}
}  // namespace

extern "C" void mem_lab_run(unsigned mode) {
  if (mode == 0) return;

  uart_puts("[mem-lab] begin\n");
  demo_pool_internal_fragmentation();
  demo_malloc_external_fragmentation();
  print_embedded_malloc_takeaways();
  uart_puts("[mem-lab] end\n");
}
