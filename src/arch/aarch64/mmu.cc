#include "arch/mmu.h"

#include <stdint.h>
#include "drivers/uart_pl011.h"

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";

static void puthex64(uint64_t v) {
  if (!v) { uart_putc('0'); return; }
  char b[16]; int i = 0;
  while (v && i < 16) { b[i++] = kHexDigits[v & 0xF]; v >>= 4; }
  while (i--) uart_putc(b[i]);
}
}  // namespace

void mmu_dump_state() {
  uint64_t mair = 0, tcr = 0, ttbr0 = 0, sctlr = 0;
  asm volatile("mrs %0, mair_el1" : "=r"(mair));
  asm volatile("mrs %0, tcr_el1" : "=r"(tcr));
  asm volatile("mrs %0, ttbr0_el1" : "=r"(ttbr0));
  asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));

  uart_puts("[mmu] mair_el1=0x"); puthex64(mair); uart_puts("\n");
  uart_puts("[mmu] tcr_el1 =0x"); puthex64(tcr); uart_puts("\n");
  uart_puts("[mmu] ttbr0_el1=0x"); puthex64(ttbr0); uart_puts("\n");
  uart_puts("[mmu] sctlr=0x"); puthex64(sctlr);
  uart_puts(" (C="); uart_putc((sctlr & (1u << 2)) ? '1' : '0');
  uart_puts(", I="); uart_putc((sctlr & (1u << 12)) ? '1' : '0');
  uart_puts(", M="); uart_putc((sctlr & (1u << 0)) ? '1' : '0');
  uart_puts(")\n");
}

void mmu_init(bool enable) {
  uart_puts("[mmu] init begin\n");
  if (!enable) {
    uart_puts("[mmu] MMU left disabled (scaffolding only)\n");
    return;
  }

  // TODO: Install translation tables and enable MMU/cache features.
  uart_puts("[mmu] enable path not yet implemented; leaving MMU off\n");
}
