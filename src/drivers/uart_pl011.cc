#include "drivers/uart_pl011.h"

namespace {
uint64_t g_uart_base = 0x09000000ull;
uint32_t g_uart_clock_hz = 24000000u;
}  // namespace

void uart_pl011_set_mmio_base(uint64_t base) {
  g_uart_base = base;
}

void uart_pl011_set_clock_hz(uint32_t hz) {
  g_uart_clock_hz = hz;
}

uint64_t uart_pl011_mmio_base(void) {
  return g_uart_base;
}

uint32_t uart_pl011_clock_hz(void) {
  return g_uart_clock_hz;
}

void uart_init() {
  const uint64_t base = uart_pl011_mmio_base();
  const uint32_t uartclk = uart_pl011_clock_hz() ? uart_pl011_clock_hz() : 24000000u;
  constexpr uint32_t baud = 115200u;

  // disable before config
  mmio_write(base + UARTCR, 0x0);
  mmio_write(base + UARTIMSC, 0x0);

  // BaudDiv = UARTCLK/(16*BAUD). PL011 registers store it as IBRD + FBRD/64.
  // BaudDiv*64 = UARTCLK*4/BAUD.
  uint64_t bauddiv_x64 = ((uint64_t)uartclk * 4ull + (baud / 2u)) / baud;
  uint32_t ibrd = static_cast<uint32_t>(bauddiv_x64 / 64ull);
  uint32_t fbrd = static_cast<uint32_t>(bauddiv_x64 % 64ull);
  if (ibrd == 0) {
    ibrd = 1;
  }

  mmio_write(base + UARTIBRD, ibrd);
  mmio_write(base + UARTFBRD, fbrd);

  // FIFO enable, 8 data bits, 1 stop, no parity
  mmio_write(base + UARTLCR_H, (1u<<4) | (3u<<5));

  // enable UART, TX, RX
  mmio_write(base + UARTCR, (1u<<0) | (1u<<8) | (1u<<9));
}

void uart_putc(char c) {
  const uint64_t base = uart_pl011_mmio_base();
  // wait while TX FIFO full
  while (mmio_read(base + UARTFR) & (1u<<5)) { }
  mmio_write(base + UARTDR, (uint32_t)c);
}

void uart_puts(const char* s) {
  while (*s) {
    if (*s == '\n') uart_putc('\r');
    uart_putc(*s++);
  }
}

void uart_print_u64(unsigned long long v) {
  char buf[32];
  int i = 0;
  if (v == 0) { uart_putc('0'); return; }
  while (v > 0 && i < 31) { buf[i++] = (char)('0' + (v % 10)); v /= 10; }
  while (i--) uart_putc(buf[i]);
}
