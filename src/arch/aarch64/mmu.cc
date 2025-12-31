#include "arch/mmu.h"

#include <stdint.h>
#include "arch/barrier.h"
#include "drivers/uart_pl011.h"

namespace {
constexpr char kHexDigits[] = "0123456789abcdef";

static void puthex64(uint64_t v) {
  if (!v) { uart_putc('0'); return; }
  char b[16]; int i = 0;
  while (v && i < 16) { b[i++] = kHexDigits[v & 0xF]; v >>= 4; }
  while (i--) uart_putc(b[i]);
}

static inline uint64_t read_sctlr_el1() {
  uint64_t v = 0;
  asm volatile("mrs %0, sctlr_el1" : "=r"(v));
  return v;
}

static inline uint64_t read_id_aa64mmfr0_el1() {
  uint64_t v = 0;
  asm volatile("mrs %0, id_aa64mmfr0_el1" : "=r"(v));
  return v;
}

static inline void write_mair_el1(uint64_t v) { asm volatile("msr mair_el1, %0" :: "r"(v)); }
static inline void write_tcr_el1(uint64_t v)  { asm volatile("msr tcr_el1, %0" :: "r"(v)); }
static inline void write_ttbr0_el1(uint64_t v){ asm volatile("msr ttbr0_el1, %0" :: "r"(v)); }
static inline void write_sctlr_el1(uint64_t v){ asm volatile("msr sctlr_el1, %0" :: "r"(v)); }

static inline void tlbi_vmalle1() { asm volatile("tlbi vmalle1"); }
static inline void ic_iallu() { asm volatile("ic iallu"); }

constexpr uint64_t kAttrIdxNormal = 0;
constexpr uint64_t kAttrIdxDevice = 1;

constexpr uint64_t kMairAttrNormalWbWa = 0xFF;  // Outer+Inner WBWA.
constexpr uint64_t kMairAttrDevice_nGnRE = 0x04;

static inline uint64_t mair_value() {
  return (kMairAttrNormalWbWa << (8 * kAttrIdxNormal)) |
         (kMairAttrDevice_nGnRE << (8 * kAttrIdxDevice));
}

static inline uint64_t tcr_value() {
  const uint64_t mmfr0 = read_id_aa64mmfr0_el1();
  uint64_t parange = mmfr0 & 0xFULL;
  if (parange > 6) parange = 5;  // Fallback to 48-bit.

  // T0SZ=16 => 48-bit VA, 4KB granule, Inner Shareable, WBWA table walks.
  uint64_t tcr = 0;
  tcr |= 16ull;                // T0SZ[5:0]
  tcr |= (1ull << 23);         // EPD1: disable TTBR1 walks
  tcr |= (1ull << 8);          // IRGN0[9:8] = 0b01 (WBWA)
  tcr |= (1ull << 10);         // ORGN0[11:10] = 0b01 (WBWA)
  tcr |= (3ull << 12);         // SH0[13:12] = 0b11 (Inner Shareable)
  tcr |= (0ull << 14);         // TG0[15:14] = 0b00 (4KB)
  tcr |= (parange << 32);      // IPS[34:32]
  return tcr;
}

static inline uint64_t pte_table(uint64_t next_table_phys) {
  return (next_table_phys & 0x0000FFFFFFFFF000ull) | 0b11ull;
}

static inline uint64_t pte_block(uint64_t phys_base, uint64_t attr_index, uint64_t sh) {
  constexpr uint64_t kAf = 1ull << 10;
  constexpr uint64_t kApKernelRw = 0ull << 6;  // EL1 RW, EL0 no access.
  return (phys_base & 0x0000FFFFFFFFF000ull) |
         0b01ull |
         (attr_index << 2) |
         kApKernelRw |
         (sh << 8) |
         kAf;
}

alignas(4096) static uint64_t g_l0[512];
alignas(4096) static uint64_t g_l1[512];

static void build_identity_map() {
  for (unsigned i = 0; i < 512; ++i) {
    g_l0[i] = 0;
    g_l1[i] = 0;
  }

  g_l0[0] = pte_table(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_l1)));

  // 0x00000000..0x3FFFFFFF: Device-nGnRE (MMIO window coverage).
  uint64_t dev = pte_block(0x00000000ull, kAttrIdxDevice, /*SH*/0);
  dev |= (1ull << 53);  // PXN
  dev |= (1ull << 54);  // UXN
  g_l1[0] = dev;

  // 0x40000000..0x7FFFFFFF: Normal cacheable WBWA, Inner Shareable.
  g_l1[1] = pte_block(0x40000000ull, kAttrIdxNormal, /*SH*/3);

  dsb_ish();
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
  if (!enable) {
    return;
  }

  const uint64_t before = read_sctlr_el1();
  if (before & 1u) {
    // Already enabled; keep current regime.
    return;
  }

  build_identity_map();

  write_mair_el1(mair_value());
  write_tcr_el1(tcr_value());
  write_ttbr0_el1(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_l0)));
  isb();

  // Ensure any stale TLB entries are removed before enabling translation.
  dsb_ish();
  tlbi_vmalle1();
  dsb_ish();
  isb();

  // Ensure I-cache is clean before enabling it under the new regime.
  ic_iallu();
  dsb_ish();
  isb();

  uint64_t sctlr = before;
  sctlr |= (1ull << 0);   // M: MMU enable
  sctlr |= (1ull << 2);   // C: data cache enable
  sctlr |= (1ull << 12);  // I: instruction cache enable
  write_sctlr_el1(sctlr);
  isb();
}
