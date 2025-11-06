#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/gicv3.h"
#include "arch/timer.h"
#include "arch/cpu_local.h"
#include "kmem.h"

extern "C" void kmain() {
  uart_init();
  uart_puts("[BOOT] UART ready\n");

  cpu_local_boot_init();

  kmem_init();

  gic_init();
  timer_init_hz(1000); // 1 kHz

  asm volatile("msr daifclr, #2"); // enable IRQ (clear I)

  uart_puts("Timer IRQ armed @1kHz\n");
  while (1) { asm volatile("wfe"); }
}
