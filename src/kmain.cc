#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/cpu_local.h"
#include "arch/gicv3.h"
#include "arch/timer.h"
#include "arch/irqflags.h"
#include "arch/ctx.h"
#include "kmem.h"
#include "thread.h"
#include "preempt.h"
#include "dma.h"

// External memory region symbols
extern "C" {
  extern char __dma_nc_start[];
  extern char __dma_nc_end[];
  extern char _heap_start[];
  extern char _heap_end[];
}

#ifndef ARM_TIMER_DIAG
#define ARM_TIMER_DIAG 1
#endif
#ifndef USE_CNTP
#define USE_CNTP 0
#endif

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";

static void uart_puthex64(uint64_t value) {
  if (value == 0) { uart_putc('0'); return; }
  char buf[16]; int idx = 0;
  while (value && idx < 16) { buf[idx++] = kHexDigits[value & 0xFu]; value >>= 4; }
  while (idx--) uart_putc(buf[idx]);
}

static inline void spin(unsigned n) { for (volatile unsigned i=0;i<n;i++){} }
} // namespace

// ---------------- Threads ----------------
static void a(void* arg);
static void b(void* arg);

// ---------------- DMA self-test globals ----------------
static uint8_t* g_dma_src_buf = nullptr;
static uint8_t* g_dma_dst_buf = nullptr;
static size_t   g_dma_len     = 0;
static volatile int g_dma_cb_seen = 0;

static void dma_test_cb(void* user, int status) {
  if (user) *reinterpret_cast<volatile int*>(user) = 1;
  if (status != 0 || !g_dma_dst_buf || !g_dma_src_buf) {
    uart_puts("[DMA FAIL]\n");
    return;
  }
  bool ok = true;
  for (size_t i = 0; i < g_dma_len; ++i) {
    if (g_dma_dst_buf[i] != g_dma_src_buf[i]) { ok = false; break; }
  }
  uart_puts(ok ? "[DMA OK]\n" : "[DMA FAIL]\n");
}

extern "C" void kmain() {
  uart_init();
  uart_puts("[build] " __DATE__ " " __TIME__ "\n");
  uart_puts("[BOOT] UART ready\n");

  uart_puts("[diag] cpu_local_boot_init begin\n");
  cpu_local_boot_init();
  uart_puts("[diag] cpu_local_boot_init end\n");

  uart_puts("[diag] kmem_init begin\n");
  kmem_init();
  uart_puts("[diag] kmem_init end\n");

  // ==============================
  // DMA Self-Test (runs synchronously)
  // ==============================
  uart_puts("[dma-selftest] D0 enter\n");
  uart_puts("[dma-selftest] dma_nc=[0x"); uart_puthex64((uint64_t)(uintptr_t)__dma_nc_start);
  uart_puts(" .. 0x"); uart_puthex64((uint64_t)(uintptr_t)__dma_nc_end);
  uart_puts("] heap=[0x"); uart_puthex64((uint64_t)(uintptr_t)_heap_start);
  uart_puts(" .. 0x"); uart_puthex64((uint64_t)(uintptr_t)_heap_end);
  uart_puts("]\n");

  constexpr size_t k_dma_test_len = 1024; // 1 KiB
  g_dma_len = k_dma_test_len;

  g_dma_src_buf = static_cast<uint8_t*>(kmem_alloc_aligned(k_dma_test_len, 4096));
  g_dma_dst_buf = static_cast<uint8_t*>(kmem_alloc_aligned(k_dma_test_len, 4096));

  if (!g_dma_src_buf || !g_dma_dst_buf) {
    uart_puts("[DMA] buffer allocation failed\n");
  } else {
    for (size_t i = 0; i < k_dma_test_len; ++i) {
      g_dma_src_buf[i] = static_cast<uint8_t>((i * 7u) & 0xFFu);
      g_dma_dst_buf[i] = 0u;
    }
    uart_puts("[dma-selftest] D1 filled\n");

    g_dma_cb_seen = 0;
    int submit = dma_submit_memcpy(g_dma_dst_buf, g_dma_src_buf, k_dma_test_len, dma_test_cb, (void*)&g_dma_cb_seen);
    if (submit != 0) {
      uart_puts("[DMA] submit failed\n");
    } else {
      uart_puts("[DMA] submit ok\n");
      dma_poll_complete();
      if (!g_dma_cb_seen) {
        bool ok = true;
        for (size_t i = 0; i < g_dma_len; ++i) {
          if (g_dma_dst_buf[i] != g_dma_src_buf[i]) { ok = false; break; }
        }
        uart_puts(ok ? "[DMA OK]\n" : "[DMA FAIL]\n");
      }
    }
  }

  // ==============================
  // Scheduler & IRQ setup
  // ==============================
  uart_puts("[diag] sched_init\n");
  sched_init();
  uart_puts("[diag] thread_create a\n");
  Thread* ta = thread_create(a, reinterpret_cast<void*>(0xA), 16 * 1024);
  uart_puts("[diag] thread_create b\n");
  Thread* tb = thread_create(b, reinterpret_cast<void*>(0xB), 16 * 1024);
  if (!ta || !tb) {
    uart_puts("[sched] thread_create failed\n");
    while (1) { asm volatile("wfe"); }
  }
  uart_puts("[diag] sched_add a\n"); sched_add(ta);
  uart_puts("[diag] sched_add b\n"); sched_add(tb);
  uart_puts("[sched] starting (coop)\n");

  uart_puts("[diag] gic_init\n");
  gic_init();

  uart_puts("[diag] timer_init_hz\n");
  timer_init_hz(1000); // 1kHz

  asm volatile("msr daifclr, #2" ::: "memory"); // Enable IRQ
  asm volatile("isb");
  uart_puts("[diag] IRQ enabled\n");

  // Enter scheduler
  sched_start();
  uart_puts("[BUG] returned to sched_start\n");
  while (1) { asm volatile("wfe"); }
}

// ----------- threads -----------
static void a(void* arg) {
  (void)arg;
  uart_puts("A");
  while (1) {
    preempt_disable();
    uart_puts("a");
    spin(50000);
    preempt_enable();
    spin(100000);
  }
}

static void b(void* arg) {
  (void)arg;
  uart_puts("B");
  while (1) {
    uart_puts("b");
    spin(120000);
  }
}
