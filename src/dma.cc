#include "dma.h"

#include <stddef.h>
#include <stdint.h>
#include "arch/barrier.h"
#include "arch/irqflags.h"
#include "drivers/uart_pl011.h"

extern "C" char __dma_nc_start[];
extern "C" char __dma_nc_end[];

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
static void puthex64(uint64_t v){
  if (!v){ uart_putc('0'); return; }
  char b[16]; int i=0; while(v && i<16){ b[i++]=kHexDigits[v&0xF]; v>>=4; }
  while(i--) uart_putc(b[i]);
}

static bool dma_cache_maint_allowed(const void* buf, size_t len) {
  if (!buf || len == 0) return false;
  uintptr_t start = reinterpret_cast<uintptr_t>(buf);
  uintptr_t end = start + (len - 1u);
  if (end < start) return false;  // overflow

  // Policy: current MMU setup maps 0x00000000..0x3FFFFFFF as Device-nGnRE and
  // 0x40000000..0x7FFFFFFF as Normal WBWA. Cache maintenance by VA must not
  // target Device/non-cacheable mappings, so we only operate on the Normal
  // region for now. If the mapping policy changes (e.g., a non-cacheable DMA
  // carveout inside the Normal window), update this classification.
  constexpr uintptr_t kNormalStart = 0x40000000ull;
  constexpr uintptr_t kNormalEnd = 0x80000000ull;  // exclusive
  return start >= kNormalStart && end < kNormalEnd;
}

static void dma_prepare_to_device(void* buf, size_t len) {
  static unsigned log_budget = 4;
  if (log_budget && len >= 64) {
    --log_budget;
    uart_puts("[dma] prepare_to_device len=");
    uart_print_u64(static_cast<unsigned long long>(len));
    uart_puts("\n");
  }

  if (dma_cache_maint_allowed(buf, len)) {
    dc_cvac_range(buf, len);  // clean to PoC
  }
  dma_wmb();
}

static void dma_complete_from_device(void* buf, size_t len) {
  static unsigned log_budget = 4;
  if (log_budget && len >= 64) {
    --log_budget;
    uart_puts("[dma] complete_from_device len=");
    uart_print_u64(static_cast<unsigned long long>(len));
    uart_puts("\n");
  }

  dma_rmb();
  if (dma_cache_maint_allowed(buf, len)) {
    dc_ivac_range(buf, len);  // invalidate to PoC
  }
}
}

struct dma_desc {
  volatile uint64_t src;
  volatile uint64_t dst;
  volatile uint64_t len;
  volatile uint64_t status;
  dma_cb_t cb;
  void* user;
  dma_desc* next;
};

static char* const g_dma_pool_end = __dma_nc_end;
static char* g_dma_pool_next = __dma_nc_start;

static dma_desc* g_pending_head = nullptr;
static dma_desc* g_pending_tail = nullptr;

static void dma_memcpy(void* dst, const void* src, size_t len){
  auto* d=(uint8_t*)dst; auto* s=(const uint8_t*)src;
  for(size_t i=0;i<len;i++){ d[i]=s[i]; }
}

static dma_desc* dma_alloc_desc(void){
  uintptr_t raw=(uintptr_t)g_dma_pool_next;
  constexpr uintptr_t align=alignof(dma_desc);
  raw=(raw+align-1u)&~(align-1u);
  char* aligned=(char*)raw;
  if (aligned + sizeof(dma_desc) > g_dma_pool_end){ return nullptr; }
  g_dma_pool_next = aligned + sizeof(dma_desc);
  return (dma_desc*)aligned;
}

extern "C" int dma_submit_memcpy(void* dst, const void* src, size_t len, dma_cb_t cb, void* user){
  if (!dst || !src || len==0){ return -1; }
  dma_desc* desc=dma_alloc_desc();
  if (!desc){ return -2; }
  desc->src=(uint64_t)(uintptr_t)src;
  desc->dst=(uint64_t)(uintptr_t)dst;
  desc->len=(uint64_t)len;
  desc->status=1u;
  desc->cb=cb;
  desc->user=user;
  desc->next=nullptr;

  dma_prepare_to_device(const_cast<void*>(src), len);
  dma_prepare_to_device(desc, sizeof(*desc));

  unsigned long flags=local_irq_save();
  dma_desc* prev_tail = g_pending_tail;
  if (prev_tail){ prev_tail->next=desc; } else { g_pending_head=desc; }
  g_pending_tail=desc;
  local_irq_restore(flags);

  if (prev_tail) {
    dma_prepare_to_device(prev_tail, sizeof(*prev_tail));
  }

  uart_puts("[DMA] queued desc=0x"); puthex64((uint64_t)(uintptr_t)desc);
  uart_puts(" next=0x"); puthex64((uint64_t)(uintptr_t)desc->next);
  uart_puts(" src=0x"); puthex64(desc->src);
  uart_puts(" dst=0x"); puthex64(desc->dst);
  uart_puts(" len=");   uart_print_u64(len);
  uart_puts("\n");
  return 0;
}

extern "C" void dma_poll_complete(void){
  while (g_pending_head){
    dma_desc* desc=g_pending_head;
    g_pending_head=desc->next;
    if (!g_pending_head){ g_pending_tail=nullptr; }
    desc->next=nullptr;

    void* dst=(void*)(uintptr_t)desc->dst;
    const void* src=(const void*)(uintptr_t)desc->src;
    size_t len=(size_t)desc->len;

    uart_puts("[DMA] poll: desc=0x"); puthex64((uint64_t)(uintptr_t)desc);
    uart_puts(" src=0x"); puthex64(desc->src);
    uart_puts(" dst=0x"); puthex64(desc->dst);
    uart_puts(" len="); uart_print_u64(len);
    uart_puts("\n");

    if (len) {
      // --- Begin non-coherent DMA model ---
      // Device reads |src| from memory: caller must have prepared it.
      dma_memcpy(dst, src, len);

      // Our "device" is software running on the CPU, so the stores above land
      // in the CPU cache. Clean to PoC to model a device that writes memory
      // directly (so a later invalidate doesn't drop dirty lines).
      if (dma_cache_maint_allowed(dst, len)) {
        dc_cvac_range(dst, len);
      }

      // CPU is about to consume device-written memory (in callbacks/tests).
      dma_complete_from_device(dst, len);
      // --- End non-coherent DMA model ---
    }

    desc->status=0u;
    dma_prepare_to_device(desc, sizeof(*desc));

    uart_puts("[DMA] done\n");
    if (desc->cb){ desc->cb(desc->user, 0); }
  }
}
