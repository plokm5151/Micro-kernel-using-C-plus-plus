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

constexpr uint64_t kPtePxN = 1ull << 53;
constexpr uint64_t kPteUxN = 1ull << 54;

constexpr uint64_t kAttrIdxNormal = 0;
constexpr uint64_t kAttrIdxDevice = 1;
constexpr uint64_t kAttrIdxNormalNc = 2;

constexpr uint64_t kMairAttrNormalWbWa = 0xFF;  // Outer+Inner WBWA.
constexpr uint64_t kMairAttrDevice_nGnRE = 0x04;
constexpr uint64_t kMairAttrNormalNc = 0x44;    // Outer+Inner Non-cacheable.

static inline uint64_t mair_value() {
  return (kMairAttrNormalWbWa << (8 * kAttrIdxNormal)) |
         (kMairAttrDevice_nGnRE << (8 * kAttrIdxDevice)) |
         (kMairAttrNormalNc << (8 * kAttrIdxNormalNc));
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

static inline uint64_t pte_page(uint64_t phys_base, uint64_t attr_index, uint64_t sh) {
  constexpr uint64_t kAf = 1ull << 10;
  constexpr uint64_t kApKernelRw = 0ull << 6;  // EL1 RW, EL0 no access.
  return (phys_base & 0x0000FFFFFFFFF000ull) |
         0b11ull |
         (attr_index << 2) |
         kApKernelRw |
         (sh << 8) |
         kAf;
}

alignas(4096) static uint64_t g_l0[512];
alignas(4096) static uint64_t g_l1[512];
alignas(4096) static uint64_t g_l2_lowram[512];
alignas(4096) static uint64_t g_l3_dma0[512];
alignas(4096) static uint64_t g_l3_dma1[512];
alignas(4096) static uint64_t g_l3_extra[8][512];
static unsigned g_l3_extra_next = 0;

extern "C" {
extern char __dma_nc_start[];
extern char __dma_nc_end[];
}

constexpr uintptr_t kL1BlockSize = 1ull << 30;  // 1GB
constexpr uintptr_t kL2BlockSize = 1ull << 21;  // 2MB
constexpr uintptr_t kPageSize = 1ull << 12;     // 4KB

// Pi4 peripherals live at the top of the first 4GB (0xFE000000..).
constexpr uintptr_t kPeriphL1Base = 0xC0000000ull;  // 3GB-aligned (L1 index 3)

#if defined(DMA_WINDOW_POLICY_NONCACHEABLE) && defined(DMA_WINDOW_POLICY_CACHEABLE)
#error "Define only one of DMA_WINDOW_POLICY_{CACHEABLE,NONCACHEABLE}"
#endif
#if !defined(DMA_WINDOW_POLICY_NONCACHEABLE) && !defined(DMA_WINDOW_POLICY_CACHEABLE)
#define DMA_WINDOW_POLICY_CACHEABLE 1
#endif

static void build_map() {
  for (unsigned i = 0; i < 512; ++i) {
    g_l0[i] = 0;
    g_l1[i] = 0;
    g_l2_lowram[i] = 0;
    g_l3_dma0[i] = 0;
    g_l3_dma1[i] = 0;
  }
  for (unsigned t = 0; t < (sizeof(g_l3_extra) / sizeof(g_l3_extra[0])); ++t) {
    for (unsigned i = 0; i < 512; ++i) {
      g_l3_extra[t][i] = 0;
    }
  }
  g_l3_extra_next = 0;

  g_l0[0] = pte_table(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_l1)));

  // VA 0x00000000..0x3FFFFFFF: low RAM (identity, cacheable) via L2.
  g_l1[0] = pte_table(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_l2_lowram)));

  // VA 0x40000000..0x7FFFFFFF: non-cacheable alias of low RAM (for lab/device view).
  g_l1[1] = pte_block(/*phys*/0x00000000ull, kAttrIdxNormalNc, /*SH*/3) | kPtePxN | kPteUxN;

  // VA 0xC0000000..0xFFFFFFFF: peripherals (Device-nGnRE).
  g_l1[3] = pte_block(static_cast<uint64_t>(kPeriphL1Base), kAttrIdxDevice, /*SH*/0) | kPtePxN | kPteUxN;

  for (unsigned i = 0; i < 512; ++i) {
    uint64_t pa = static_cast<uint64_t>(i) * static_cast<uint64_t>(kL2BlockSize);
    g_l2_lowram[i] = pte_block(pa, kAttrIdxNormal, /*SH*/3);
  }

  const uintptr_t dma_start = reinterpret_cast<uintptr_t>(__dma_nc_start);
  const uintptr_t dma_end = reinterpret_cast<uintptr_t>(__dma_nc_end);
  if (dma_end > dma_start) {
    const uintptr_t dma_last = dma_end - 1u;
    if ((dma_start / kL1BlockSize) == 0 && (dma_last / kL1BlockSize) == 0) {
      uintptr_t dma_first_block = dma_start & ~(kL2BlockSize - 1u);
      uintptr_t dma_last_block = dma_last & ~(kL2BlockSize - 1u);

      uintptr_t blocks[2] = {dma_first_block, dma_last_block};
      uint64_t* l3_tables[2] = {g_l3_dma0, g_l3_dma1};
      unsigned table_count = (dma_first_block == dma_last_block) ? 1u : 2u;

      for (unsigned t = 0; t < table_count; ++t) {
        uintptr_t block_base = blocks[t];
        unsigned l2_index = static_cast<unsigned>(block_base / kL2BlockSize);
        uint64_t* l3 = l3_tables[t];

        for (unsigned i = 0; i < 512; ++i) {
          uintptr_t pa = block_base + (static_cast<uintptr_t>(i) * kPageSize);
          uint64_t attr = kAttrIdxNormal;
          uint64_t xn = 0;

          if (pa >= dma_start && pa < dma_end) {
            xn = kPtePxN | kPteUxN;
#if defined(DMA_WINDOW_POLICY_NONCACHEABLE)
            attr = kAttrIdxNormalNc;
#endif
          }
          l3[i] = pte_page(static_cast<uint64_t>(pa), attr, /*SH*/3) | xn;
        }

        g_l2_lowram[l2_index] = pte_table(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(l3)));
      }
    }
  }

  dsb_ish();
}

static inline uintptr_t align_down(uintptr_t v, uintptr_t a) {
  return v & ~(a - 1u);
}

static uint64_t* ensure_l3_for_lowram_block(unsigned l2_index) {
  if (l2_index >= 512) return nullptr;
  uint64_t e = g_l2_lowram[l2_index];
  const uint64_t type = e & 0b11ull;

  if (type == 0b11ull) {
    uintptr_t table_phys = static_cast<uintptr_t>(e & 0x0000FFFFFFFFF000ull);
    return reinterpret_cast<uint64_t*>(table_phys);
  }
  if (type != 0b01ull) {
    return nullptr;
  }
  if (g_l3_extra_next >= (sizeof(g_l3_extra) / sizeof(g_l3_extra[0]))) {
    return nullptr;
  }

  uint64_t* l3 = g_l3_extra[g_l3_extra_next++];
  const uint64_t attr_index = (e >> 2) & 0x7ull;
  const uint64_t sh = (e >> 8) & 0x3ull;
  const uint64_t xn = e & (kPtePxN | kPteUxN);

  const uintptr_t block_base = static_cast<uintptr_t>(l2_index) * kL2BlockSize;
  for (unsigned i = 0; i < 512; ++i) {
    uintptr_t pa = block_base + (static_cast<uintptr_t>(i) * kPageSize);
    l3[i] = pte_page(static_cast<uint64_t>(pa), attr_index, sh) | xn;
  }
  g_l2_lowram[l2_index] = pte_table(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(l3)));
  return l3;
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

  build_map();

  write_mair_el1(mair_value());
  write_tcr_el1(tcr_value());
  write_ttbr0_el1(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(g_l0)));
  isb();

  dsb_ish();
  tlbi_vmalle1();
  dsb_ish();
  isb();

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

int mmu_enabled(void) {
  return (read_sctlr_el1() & 1u) != 0;
}

int mmu_guard_page(void* addr) {
  if (!addr) return -1;
  if (!mmu_enabled()) return -1;

  uintptr_t va = align_down(reinterpret_cast<uintptr_t>(addr), kPageSize);
  if (va >= kL1BlockSize) return -1;

  const unsigned l2_index = static_cast<unsigned>(va / kL2BlockSize);
  const unsigned l3_index = static_cast<unsigned>((va % kL2BlockSize) / kPageSize);

  uint64_t* l3 = ensure_l3_for_lowram_block(l2_index);
  if (!l3) return -1;

  l3[l3_index] = 0;
  dsb_ish();
  tlbi_vmalle1();
  dsb_ish();
  isb();
  return 0;
}
