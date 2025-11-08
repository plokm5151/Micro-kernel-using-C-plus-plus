#include "drivers/uart_pl011.h"
#include "arch/timer.h"

#include <stdint.h>

#ifndef USE_CNTP
#define USE_CNTP 1
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

inline uint64_t read_cntfrq() {
  uint64_t value = 0;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(value));
  return value;
}

inline uint64_t read_daif() {
  uint64_t value = 0;
  asm volatile("mrs %0, daif" : "=r"(value));
  return value;
}

inline void write_cntp_tval(uint64_t value) {
  asm volatile("msr cntp_tval_el0, %0" :: "r"(value));
}

inline void write_cntp_ctl(uint64_t value) {
  asm volatile("msr cntp_ctl_el0, %0" :: "r"(value));
  asm volatile("isb");
}

inline uint64_t read_cntp_ctl() {
  uint64_t value = 0;
  asm volatile("mrs %0, cntp_ctl_el0" : "=r"(value));
  return value;
}

inline void write_cntv_tval(uint64_t value) {
  asm volatile("msr cntv_tval_el0, %0" :: "r"(value));
}

inline void write_cntv_ctl(uint64_t value) {
  asm volatile("msr cntv_ctl_el0, %0" :: "r"(value));
  asm volatile("isb");
}

inline uint64_t read_cntv_ctl() {
  uint64_t value = 0;
  asm volatile("mrs %0, cntv_ctl_el0" : "=r"(value));
  return value;
}

inline void write_timer_tval(uint64_t value) {
#if USE_CNTP
  write_cntp_tval(value);
#else
  write_cntv_tval(value);
#endif
}

inline void write_timer_ctl(uint64_t value) {
#if USE_CNTP
  write_cntp_ctl(value);
#else
  write_cntv_ctl(value);
#endif
}

inline uint64_t read_timer_ctl() {
#if USE_CNTP
  return read_cntp_ctl();
#else
  return read_cntv_ctl();
#endif
}

#if ARM_TIMER_DIAG
bool g_timer_diag_once = false;
#endif

uint64_t compute_ticks(uint32_t hz) {
  uint64_t freq = read_cntfrq();
  uint64_t ticks = (hz == 0) ? 0 : (freq / hz);
  if (ticks == 0) {
    ticks = 1;
  }
  return ticks;
}
}  // namespace

void timer_init_hz(uint32_t hz) {
  if (!hz) {
    return;
  }

  const uint64_t ticks = compute_ticks(hz);

  write_timer_ctl(0);        // disable & unmask
  write_timer_tval(ticks);   // program next expiry
  write_timer_ctl(1);        // ENABLE=1, IMASK=0

#if ARM_TIMER_DIAG
  if (!g_timer_diag_once) {
    g_timer_diag_once = true;
    const uint64_t ctl = read_timer_ctl();
    const uint64_t daif = read_daif();
    uart_puts("[timer] ctl=0x");
    uart_puthex64(ctl);
    uart_puts(" daif=0x");
    uart_puthex64(daif);
    uart_puts(" use=");
#if USE_CNTP
    uart_puts("CNTP\n");
#else
    uart_puts("CNTV\n");
#endif
    if ((ctl & 1u) == 0u) {
      uart_puts("[timer] enable bit appears 0\n");
    }
  }
#endif

  if (hz == 1000u) {
    uart_puts("Timer IRQ armed @1kHz\n");
  }
}

void timer_irq() {
  static unsigned heartbeat = 0;

  const uint64_t ticks = compute_ticks(1000u);
  write_timer_tval(ticks);

  uart_putc('.');
  heartbeat++;
  if ((heartbeat & 63u) == 0u) {
    uart_putc('\n');
  }
}
