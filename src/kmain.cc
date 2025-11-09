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

#ifndef TEST_SGI_ON_BOOT
#define TEST_SGI_ON_BOOT 1
#endif
#ifndef ARM_TIMER_DIAG
#define ARM_TIMER_DIAG 1
#endif

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

#if ARM_TIMER_DIAG
bool g_probe_once = false;
inline uint64_t read_cntp_ctl_diag() {
  uint64_t v = 0;
  asm volatile("mrs %0, cntp_ctl_el0" : "=r"(v));
  return v;
}
inline uint64_t read_cntv_ctl_diag() {
  uint64_t v = 0;
  asm volatile("mrs %0, cntv_ctl_el0" : "=r"(v));
  return v;
}
#endif
}  // namespace

static uint8_t* g_dma_src_buf = nullptr;
static uint8_t* g_dma_dst_buf = nullptr;
static size_t g_dma_len = 0;
static volatile int g_dma_cb_seen = 0;

void a(void*);
void b(void*);

static void dma_test_cb(void* user, int status) {
  if (user) {
    *reinterpret_cast<volatile int*>(user) = 1;
  }
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

  uart_puts("[sched] starting (coop)\n");

  // ---- 先初始化 GIC/Timer 並開 IRQ，確保 CI 能看到 Timer 訊息 ----
  uart_puts("[diag] gic_init\n");
  gic_init();
  uart_puts("[diag] timer_init_hz\n");
  timer_init_hz(1000); // 1 kHz

  asm volatile("msr daifclr, #2"); // enable IRQ (clear I)
  asm volatile("isb");
  uart_puts("[diag] IRQ enabled\n");

  // ---- DMA 自測（不依賴 IRQ 時序，也會同步確認）----
  {
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
      g_dma_cb_seen = 0;
      const volatile void* cb_user_const = &g_dma_cb_seen;
      void* cb_user = const_cast<void*>(cb_user_const);
      int submit = dma_submit_memcpy(g_dma_dst_buf, g_dma_src_buf, k_dma_test_len,
                                     dma_test_cb, cb_user);
      if (submit != 0) {
        uart_puts("[DMA] submit failed\n");
      } else {
        uart_puts("[DMA] submit ok\n");
        dma_poll_complete();
        if (!g_dma_cb_seen) {
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
          g_dma_cb_seen = 1;
        }
      }
    }
  }

#if TEST_SGI_ON_BOOT
  {
    uint64_t sgi = 0;
    const uint64_t intid = 1ull;       // SGI #1
    const uint64_t target_list = 1ull; // current CPU (bit0)
    sgi |= (intid & 0xFu) << 24;
    sgi |= (target_list & 0xFFFFu);
    asm volatile("msr ICC_SGI1R_EL1, %0" :: "r"(sgi) : "memory");
    asm volatile("isb");
  }
#endif

#if ARM_TIMER_DIAG
  if (!g_probe_once) {
    g_probe_once = true;
    for (volatile int i = 0; i < 10000; ++i) {
      asm volatile("" ::: "memory");
    }
    const uint64_t cntp_ctl = read_cntp_ctl_diag();
    const uint64_t cntv_ctl = read_cntv_ctl_diag();
    unsigned long irq_flags = local_irq_save();
    uart_puts("[probe] cntp_ctl=0x");
    uart_puthex64(cntp_ctl);
    uart_puts(" cntv_ctl=0x");
    uart_puthex64(cntv_ctl);
    uart_puts("\n");
    local_irq_restore(irq_flags);
  }
#endif

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
