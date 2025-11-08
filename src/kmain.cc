#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/cpu_local.h"
#include "arch/gicv3.h"
#include "arch/timer.h"
#include "arch/ctx.h"
#include "kmem.h"
#include "thread.h"
#include "preempt.h"
#include "dma.h"

static uint8_t* g_dma_src_buf = nullptr;
static uint8_t* g_dma_dst_buf = nullptr;
static size_t g_dma_len = 0;

void a(void*);
void b(void*);

static void dma_test_cb(void* user, int status) {
  (void)user;
  if (status != 0 || !g_dma_dst_buf || !g_dma_src_buf) {
    uart_puts("[DMA FAIL]\n");
    return;
  }
  bool ok = true;
  for (size_t i = 0; i < g_dma_len; ++i) {
    if (g_dma_dst_buf[i] != g_dma_src_buf[i]) {
      ok = false;
      break;
    }
  }
  if (ok) {
    uart_puts("[DMA OK]\n");
  } else {
    uart_puts("[DMA FAIL]\n");
  }
}

extern "C" void kmain() {
  uart_init();
  uart_puts("[BOOT] UART ready\n");

  uart_puts("[diag] cpu_local_boot_init begin\n");
  cpu_local_boot_init();
  uart_puts("[diag] cpu_local_boot_init end\n");

  uart_puts("[diag] kmem_init begin\n");
  kmem_init();
  uart_puts("[diag] kmem_init end\n");

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
  uart_puts("[diag] sched_add a\n");
  sched_add(ta);
  uart_puts("[diag] sched_add b\n");
  sched_add(tb);

  uart_puts("[diag] gic_init\n");
  gic_init();
  uart_puts("[diag] timer_init_hz\n");
  timer_init_hz(1000); // 1 kHz

  asm volatile("msr daifclr, #2"); // enable IRQ (clear I)

  uart_puts("[diag] IRQ enabled\n");

  constexpr size_t k_dma_test_len = 4096;
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
    int submit = dma_submit_memcpy(g_dma_dst_buf, g_dma_src_buf, k_dma_test_len,
                                   dma_test_cb, nullptr);
    if (submit != 0) {
      uart_puts("[DMA] submit failed\n");
    }
  }

  uart_puts("[sched] starting (coop)\n");
  sched_start(); // 不返回
  while (1) { asm volatile("wfe"); }
}

static inline void spin(unsigned n) {
  for (volatile unsigned i = 0; i < n; i++) {
  }
}

void a(void* arg) {
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

void b(void* arg) {
  (void)arg;
  uart_puts("B");
  while (1) {
    uart_puts("b");
    spin(120000);
  }
}
