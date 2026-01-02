
#pragma once
#include <stdint.h>

#define UARTDR       0x00
#define UARTFR       0x18
#define UARTIBRD     0x24
#define UARTFBRD     0x28
#define UARTLCR_H    0x2C
#define UARTCR       0x30
#define UARTIMSC     0x38
static inline void mmio_write(uint64_t addr, uint32_t val){ *(volatile uint32_t*)(addr)=val; }
static inline uint32_t mmio_read(uint64_t addr){ return *(volatile uint32_t*)(addr); }

// Platform-provided configuration. Defaults are set for QEMU virt.
void uart_pl011_set_mmio_base(uint64_t base);
void uart_pl011_set_clock_hz(uint32_t hz);
uint64_t uart_pl011_mmio_base(void);
uint32_t uart_pl011_clock_hz(void);

void uart_init();
void uart_putc(char c);
void uart_puts(const char* s);
void uart_print_u64(unsigned long long v);
