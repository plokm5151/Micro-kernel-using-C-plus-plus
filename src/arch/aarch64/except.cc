#include "drivers/uart_pl011.h"
#include "arch/except.h"
#include <stdint.h>

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";
static void puthex64(uint64_t v){
  if (!v){ uart_putc('0'); return; }
  char b[16]; int i=0; while(v && i<16){ b[i++]=kHexDigits[v&0xF]; v>>=4; }
  while(i--) uart_putc(b[i]);
}
static void puthex32(uint32_t v){
  if (!v){ uart_putc('0'); return; }
  char b[8]; int i=0; while(v && i<8){ b[i++]=kHexDigits[v&0xF]; v>>=4; }
  while(i--) uart_putc(b[i]);
}
static const char* ec_name(uint32_t ec){
  switch(ec){
    case 0x15: return "SVC64";
    case 0x18: return "IABT";
    case 0x24: return "DABT";
    case 0x26: return "SP alignment";
    case 0x2f: return "SError";
    case 0x00: return "Unknown";
    default: return "?";
  }
}
}

extern "C" void except_el1_sync(uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr, uint64_t tag){
  uint32_t ec = (uint32_t)((esr >> 26) & 0x3F);
  uart_puts("\n[EXC] EL1 sync tag=0x"); puthex64(tag);
  uart_puts(" EC=0x"); puthex32(ec); uart_puts(" ("); uart_puts(ec_name(ec)); uart_puts(")");
  uart_puts(" ESR=0x"); puthex64(esr);
  uart_puts(" ELR=0x"); puthex64(elr);
  uart_puts(" FAR=0x"); puthex64(far);
  uart_puts(" SPSR=0x"); puthex64(spsr);
  uart_puts("\n");
  // 停機等待收集 log
  while (1) { asm volatile("wfe"); }
}
