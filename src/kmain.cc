#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/cpu_local.h"
#include "arch/gicv3.h"
#include "arch/timer.h"
#include "arch/ctx.h"
#include "kmem.h"
#include "thread.h"
#include "preempt.h"

void a(void*);
void b(void*);

extern "C" void kmain() {
  uart_init();
  uart_puts("[BOOT] UART ready\n");

  cpu_local_boot_init();

  kmem_init();

  sched_init();
  Thread* ta = thread_create(a, reinterpret_cast<void*>(0xA), 16 * 1024);
  Thread* tb = thread_create(b, reinterpret_cast<void*>(0xB), 16 * 1024);
  if (!ta || !tb) {
    uart_puts("[sched] thread_create failed\n");
    while (1) { asm volatile("wfe"); }
  }
  sched_add(ta);
  sched_add(tb);

  gic_init();
  timer_init_hz(1000); // 1 kHz

  asm volatile("msr daifclr, #2"); // enable IRQ (clear I)

  uart_puts("Timer IRQ armed @1kHz\n");
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
