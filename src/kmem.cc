// Bump allocator for early boot; not IRQ/multi-CPU safe.
// Alignment must be a power of two; we auto-round-up.
#include <stdint.h>
#include "kmem.h"
extern "C" char _heap_start[], _heap_end[];
static uintptr_t cur, end;
static inline uintptr_t align_up(uintptr_t v, uintptr_t a){ return (v + a - 1) & ~(a-1); }
static inline bool is_pow2(uintptr_t a){ return a && ((a & (a - 1)) == 0); }
static inline uintptr_t round_up_pow2(uintptr_t a){
  if (a <= 1) return 1;
  a--;
  a |= a >> 1;
  a |= a >> 2;
  a |= a >> 4;
  a |= a >> 8;
  a |= a >> 16;
#if UINTPTR_MAX > 0xffffffffu
  a |= a >> 32;
#endif
  return a + 1;
}
extern "C" void kmem_init(void){ cur=(uintptr_t)_heap_start; end=(uintptr_t)_heap_end; }
extern "C" void* kmem_alloc_aligned(size_t sz, size_t align){
  if (align < 16) align = 16;
  if (!is_pow2((uintptr_t)align)) align = (size_t)round_up_pow2((uintptr_t)align);
  uintptr_t p=align_up(cur, (uintptr_t)align);
  if (p+sz > end) return nullptr; cur = p+sz; return (void*)p;
}
