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

void uart_puthex64(uint64_t value) {
  if (value == 0) {
    uart_putc('0');
    return;
  }
  char buf[16];
  int idx = 0;
  while (value != 0 && idx < 16) {
    buf[idx++] = kHexDigits[value & 0xFu];
    value >>= 4;
  }
  while (idx--) {
    uart_putc(buf[idx]);
  }
}
}  // namespace

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

static void dma_memcpy(void* dst, const void* src, size_t len) {
  auto* d = static_cast<uint8_t*>(dst);
  const auto* s = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static dma_desc* dma_alloc_desc(void) {
  uintptr_t raw = reinterpret_cast<uintptr_t>(g_dma_pool_next);
  constexpr uintptr_t align = alignof(dma_desc);
  raw = (raw + align - 1u) & ~(align - 1u);
  char* aligned = reinterpret_cast<char*>(raw);
  if (aligned + sizeof(dma_desc) > g_dma_pool_end) {
    return nullptr;
  }
  g_dma_pool_next = aligned + sizeof(dma_desc);
  return reinterpret_cast<dma_desc*>(aligned);
}

extern "C" int dma_submit_memcpy(void* dst, const void* src, size_t len,
                                  dma_cb_t cb, void* user) {
  if (!dst || !src || len == 0) {
    return -1;
  }
  dma_desc* desc = dma_alloc_desc();
  if (!desc) {
    return -2;
  }
  desc->src = reinterpret_cast<uint64_t>(src);
  desc->dst = reinterpret_cast<uint64_t>(dst);
  desc->len = static_cast<uint64_t>(len);
  desc->status = 1u;  // pending
  desc->cb = cb;
  desc->user = user;
  desc->next = nullptr;

  unsigned long flags = local_irq_save();
  if (g_pending_tail) {
    g_pending_tail->next = desc;
  } else {
    g_pending_head = desc;
  }
  g_pending_tail = desc;
  local_irq_restore(flags);

  dmb_ish();
  dc_civac_range(desc, sizeof(*desc));

  uart_puts("[DMA] queued desc=0x");
  uart_puthex64(reinterpret_cast<uint64_t>(desc));
  uart_puts(" src=0x");
  uart_puthex64(desc->src);
  uart_puts(" dst=0x");
  uart_puthex64(desc->dst);
  uart_puts(" len=");
  uart_print_u64(len);
  uart_puts("\n");
  return 0;
}

extern "C" void dma_poll_complete(void) {
  while (g_pending_head) {
    dma_desc* desc = g_pending_head;
    g_pending_head = desc->next;
    if (!g_pending_head) {
      g_pending_tail = nullptr;
    }
    desc->next = nullptr;

    void* dst = reinterpret_cast<void*>(static_cast<uintptr_t>(desc->dst));
    const void* src = reinterpret_cast<const void*>(static_cast<uintptr_t>(desc->src));
    size_t len = static_cast<size_t>(desc->len);

    uart_puts("[DMA] poll: copy len=");
    uart_print_u64(len);
    uart_puts(" src=0x");
    uart_puthex64(desc->src);
    uart_puts(" dst=0x");
    uart_puthex64(desc->dst);
    uart_puts("\n");

    if (len) {
      dma_memcpy(dst, src, len);
      dmb_ish();
      dc_civac_range(dst, len);
    } else {
      dmb_ish();
    }

    desc->status = 0u;
    dmb_ish();
    dc_civac_range(desc, sizeof(*desc));

    uart_puts("[DMA] done\n");
    if (desc->cb) {
      desc->cb(desc->user, 0);
    }
  }
}
