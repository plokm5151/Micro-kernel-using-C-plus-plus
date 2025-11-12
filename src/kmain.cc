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
#ifndef DMA_SELFTEST_AFTER_IRQ
#define DMA_SELFTEST_AFTER_IRQ 0   // 0: 早期執行抓錯；1: 開 IRQ 後執行，利於 CI 穩定
#endif

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
static void puthex64(uint64_t v){
  if (!v){ uart_putc('0'); return; }
  char b[16]; int i=0; while(v && i<16){ b[i++]=kHexDigits[v&0xF]; v>>=4; }
  while(i--) uart_putc(b[i]);
}
static inline void spin(unsigned n){ for(volatile unsigned i=0;i<n;i++){} }
static inline int in_range(uintptr_t p, uintptr_t lo, uintptr_t hi){ return (p >= lo) && (p < hi); }

static void dump_early_probe(){
  uint64_t el=0,vbar=0,daif=0,cntv=0,cntp=0;
  asm volatile("mrs %0, CurrentEL" : "=r"(el));
  asm volatile("mrs %0, VBAR_EL1"  : "=r"(vbar));
  asm volatile("mrs %0, daif"      : "=r"(daif));
  asm volatile("mrs %0, cntv_ctl_el0" : "=r"(cntv));
  asm volatile("mrs %0, cntp_ctl_el0" : "=r"(cntp));
  uart_puts("[diag] ---- early ---- EL="); puthex64(el);
  uart_puts(" VBAR="); puthex64(vbar);
  uart_puts(" DAIF="); puthex64(daif);
  uart_puts(" CNTV_CTL="); puthex64(cntv);
  uart_puts(" CNTP_CTL="); puthex64(cntp);
  uart_puts("\n");
}

// ---------------- Threads ----------------
static void a(void*); static void b(void*);

// ---------------- DMA self-test globals ----------------
static uint8_t* g_src=nullptr; static uint8_t* g_dst=nullptr; static size_t g_len=0;
static volatile int g_cb_seen=0;

static void dma_test_cb(void* user, int status){
  if (user) *reinterpret_cast<volatile int*>(user)=1;
  bool ok = (status==0) && g_src && g_dst;
  if (ok){
    for(size_t i=0;i<g_len;i++){ if(g_dst[i]!=g_src[i]){ ok=false; break; } }
  }
  uart_puts(ok?"[DMA OK]\n":"[DMA FAIL]\n");
}

static void run_dma_selftest(){
  uart_puts("[dma-selftest] D0 enter\n");
  uart_puts("[dma-selftest] dma_nc=[0x"); puthex64((uint64_t)(uintptr_t)__dma_nc_start);
  uart_puts(" .. 0x"); puthex64((uint64_t)(uintptr_t)__dma_nc_end);
  uart_puts("] heap=[0x"); puthex64((uint64_t)(uintptr_t)_heap_start);
  uart_puts(" .. 0x"); puthex64((uint64_t)(uintptr_t)_heap_end);
  uart_puts("]\n");

  constexpr size_t klen=1024;
  g_len=klen;

  uart_puts("[dma-selftest] D0.1 before alloc\n");
  g_src = static_cast<uint8_t*>(kmem_alloc_aligned(klen, 4096));
  g_dst = static_cast<uint8_t*>(kmem_alloc_aligned(klen, 4096));
  uart_puts("[dma-selftest] D0.2 after alloc src=0x"); puthex64((uint64_t)(uintptr_t)g_src);
  uart_puts(" dst=0x"); puthex64((uint64_t)(uintptr_t)g_dst); uart_puts("\n");

  if (!g_src || !g_dst){
    uart_puts("[DMA] buffer allocation failed\n");
    return;
  }

  // 位址範圍檢查
  uintptr_t hs=(uintptr_t)_heap_start, he=(uintptr_t)_heap_end;
  if (!in_range((uintptr_t)g_src, hs, he) || !in_range((uintptr_t)g_dst, hs, he)){
    uart_puts("[DMA][warn] src/dst not in heap range!\n");
  }

  for(size_t i=0;i<klen;i++){ g_src[i] = (uint8_t)((i*7u)&0xFFu); g_dst[i]=0; }
  uart_puts("[dma-selftest] D1 filled\n");

  g_cb_seen=0;
  int submit = dma_submit_memcpy(g_dst, g_src, klen, dma_test_cb, (void*)&g_cb_seen);
  if (submit != 0){
    uart_puts("[DMA] submit failed\n");
    return;
  }
  uart_puts("[DMA] submit ok\n");

  dma_poll_complete(); // 同步處理一次
  if (!g_cb_seen){
    bool ok=true; for(size_t i=0;i<g_len;i++){ if(g_dst[i]!=g_src[i]){ ok=false; break; } }
    uart_puts(ok?"[DMA OK]\n":"[DMA FAIL]\n");
  }
}

} // namespace

extern "C" void kmain(){
  uart_init();
  uart_puts("[build] " __DATE__ " " __TIME__ "\n");
  uart_puts("[BOOT] UART ready\n");

  dump_early_probe();

  uart_puts("[diag] cpu_local_boot_init begin\n"); cpu_local_boot_init(); uart_puts("[diag] cpu_local_boot_init end\n");
  uart_puts("[diag] kmem_init begin\n");           kmem_init();           uart_puts("[diag] kmem_init end\n");

#if DMA_SELFTEST_AFTER_IRQ==0
  run_dma_selftest();
#endif

  uart_puts("[diag] sched_init\n");       sched_init();
  uart_puts("[diag] thread_create a\n");  Thread* ta = thread_create(a, (void*)0xA, 16*1024);
  uart_puts("[diag] thread_create b\n");  Thread* tb = thread_create(b, (void*)0xB, 16*1024);
  if(!ta||!tb){ uart_puts("[sched] thread_create failed\n"); while(1){ asm volatile("wfe"); } }
  uart_puts("[diag] sched_add a\n"); sched_add(ta);
  uart_puts("[diag] sched_add b\n"); sched_add(tb);
  uart_puts("[sched] starting (coop)\n");

  uart_puts("[diag] gic_init\n");      gic_init();
  uart_puts("[diag] timer_init_hz\n"); timer_init_hz(1000);

  asm volatile("msr daifclr, #2" ::: "memory"); asm volatile("isb");
  uart_puts("[diag] IRQ enabled\n");

#if DMA_SELFTEST_AFTER_IRQ==1
  run_dma_selftest();
#endif

  sched_start();
  uart_puts("[BUG] returned to sched_start\n");
  while(1){ asm volatile("wfe"); }
}

// threads
static void a(void*){
  uart_puts("A");
  while(1){
    preempt_disable(); uart_puts("a"); spin(50000); preempt_enable(); spin(100000);
  }
}
static void b(void*){
  uart_puts("B");
  while(1){ uart_puts("b"); spin(120000); }
}
