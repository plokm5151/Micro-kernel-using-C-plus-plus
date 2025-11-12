#include <stdint.h>
#include "arch/gicv3.h"
#include "drivers/uart_pl011.h"

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";

void uart_puthex32(uint32_t value) {
  if (value == 0) {
    uart_putc('0');
    return;
  }
  char buf[8];
  int idx = 0;
  while (value != 0 && idx < 8) {
    buf[idx++] = kHexDigits[value & 0xFu];
    value >>= 4;
  }
  while (idx--) {
    uart_putc(buf[idx]);
  }
}

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

inline void enable_sre_el1() {
  uint64_t v = 0;
  asm volatile("mrs %0, ICC_SRE_EL1" : "=r"(v));
  v |= 1ull; // SRE enable
  asm volatile("msr ICC_SRE_EL1, %0" :: "r"(v));
  asm volatile("isb");
}

inline void gicr_wake() {
  uint32_t w = mmio_r32(GICR_BASE + 0x0014);  // GICR_WAKER
  w &= ~(1u << 1);                            // ProcessorSleep = 0
  mmio_w32(GICR_BASE + 0x0014, w);
  while (mmio_r32(GICR_BASE + 0x0014) & (1u << 2)) {
    // Wait until ChildrenAsleep is cleared
  }
}

inline uint64_t read_icc_pmr() {
  uint64_t v = 0;
  asm volatile("mrs %0, ICC_PMR_EL1" : "=r"(v));
  return v;
}

inline uint64_t read_icc_igrpen1() {
  uint64_t v = 0;
  asm volatile("mrs %0, ICC_IGRPEN1_EL1" : "=r"(v));
  return v;
}
}  // namespace

void gic_init() {
  gicr_wake();

  // ---- SGI/PPI registers are in the SGI_base frame (GICR_BASE + 0x10000) ----
  // Group all PPIs/SGIs to Non-secure Group1
  mmio_w32(GICR_SGI_BASE + 0x0080, 0xFFFFFFFFu); // GICR_IGROUPR0
  mmio_w32(GICR_SGI_BASE + 0x0D00, 0x00000000u); // GICR_IGRPMODR0: NS Group1

  // Level-triggered for PPIs: **GICR_ICFGR1 is at 0x0C04 (NOT 0x00C4)**
  mmio_w32(GICR_SGI_BASE + 0x0C04, 0x00000000u); // GICR_ICFGR1

  // Enable SGI #1 plus the selected timer PPI
#if USE_CNTP
  mmio_w32(GICR_SGI_BASE + 0x0100, (1u << 1) | (1u << 30));
#else
  mmio_w32(GICR_SGI_BASE + 0x0100, (1u << 1) | (1u << 27));
#endif

  // Priority for INTIDs 1/27/30 (one byte per INTID from 0..31)
  volatile uint8_t* prio = (volatile uint8_t*)(GICR_SGI_BASE + 0x0400);
  prio[1]  = 0x80;
  prio[27] = 0x80;
  prio[30] = 0x80;

  // Distributor: enable Group1NS
  mmio_w32(GICD_BASE + 0x0000, (1u << 1)); // GICD_CTLR.EnableGrp1NS

  // CPU interface: sysregs path
  enable_sre_el1();
  asm volatile("msr ICC_PMR_EL1, %0" :: "r"(0xFFull)); // unmask all priorities
  asm volatile("msr ICC_BPR1_EL1, %0" :: "r"(0ull));   // no binning
  asm volatile("msr ICC_IGRPEN1_EL1, %0" :: "r"(1ull));// enable Group1
  asm volatile("isb");

  const uint32_t group = mmio_r32(GICR_SGI_BASE + 0x0080);
  const uint32_t modr  = mmio_r32(GICR_SGI_BASE + 0x0D00);
  const uint32_t isen  = mmio_r32(GICR_SGI_BASE + 0x0100);
  uart_puts("[gicr] group=0x"); uart_puthex32(group);
  uart_puts(" modr=0x");        uart_puthex32(modr);
  uart_puts(" isen=0x");        uart_puthex32(isen);
  uart_puts("\n");

  const uint32_t ctlr = mmio_r32(GICD_BASE + 0x0000);
  uart_puts("[gicd] ctlr=0x"); uart_puthex32(ctlr); uart_puts("\n");

  const uint64_t pmr  = read_icc_pmr();
  const uint64_t grp1 = read_icc_igrpen1();
  uart_puts("[icc] pmr=0x"); uart_puthex64(pmr);
  uart_puts(" grp1=0x");      uart_puthex64(grp1);
  uart_puts("\n");

  uart_puts("[gic] init done\n");
}

uint32_t gic_ack() {
  uint64_t iar = 0;
  asm volatile("mrs %0, ICC_IAR1_EL1" : "=r"(iar));
  return (uint32_t)iar;
}

void gic_eoi(uint32_t iar) {
  asm volatile("msr ICC_EOIR1_EL1, %0" :: "r"((uint64_t)iar));
  asm volatile("msr ICC_DIR_EL1, %0"   :: "r"((uint64_t)iar));
}
