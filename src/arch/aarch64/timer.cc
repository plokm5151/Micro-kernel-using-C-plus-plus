#include "drivers/uart_pl011.h"
#include "arch/timer.h"

#include <stdint.h>

static inline uint64_t read_cntfrq() {
  uint64_t value = 0;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}

static inline void write_cntv_tval(uint64_t value) {
  asm volatile("msr cntv_tval_el0, %0" :: "r"(value));
}

static inline void write_cntv_ctl(uint64_t value) {
  asm volatile("msr cntv_ctl_el0, %0" :: "r"(value));
  asm volatile("isb");
}

void timer_init_hz(uint32_t hz) {
  if (!hz) {
    return;
  }
  uint64_t freq = read_cntfrq();
  uint64_t ticks = freq / hz;
  if (ticks == 0) {
    ticks = 1;  // ensure timer fires
  }

  write_cntv_ctl(0);        // disable & unmask
  write_cntv_tval(ticks);   // program next expiry
  write_cntv_ctl(1);        // ENABLE=1, IMASK=0

  if (hz == 1000u) {
    uart_puts("Timer IRQ armed @1kHz\n");
  }
}

void timer_irq() {
  uint64_t freq = read_cntfrq();
  uint64_t ticks = freq / 1000u;
  if (ticks == 0) {
    ticks = 1;
  }
  write_cntv_tval(ticks);
  uart_puts(".");
}
