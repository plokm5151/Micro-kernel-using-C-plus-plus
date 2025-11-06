#include "arch/barrier.h"

#include <cstddef>
#include <cstdint>

namespace {

template <typename T>
static inline T align_down(T value, T alignment) {
  return value & ~(alignment - 1);
}

template <typename T>
static inline T align_up(T value, T alignment) {
  return (value + alignment - 1) & ~(alignment - 1);
}

// CTR_EL0.DminLine stores the log2(line words) for the data cache using
// 4-byte words. This guarantees the resulting line size is a power of two and
// equals 4 << DminLine bytes.
static size_t dcache_line_size() {
  uint64_t ctr = 0;
  asm volatile("mrs %0, ctr_el0" : "=r"(ctr));
  size_t dminline = static_cast<size_t>((ctr >> 16) & 0xF);
  size_t line_size = 4u << dminline;
  if (line_size == 0) {
    line_size = 64;
  }
  return line_size;
}

static void dc_cvac_line(uintptr_t addr) {
  asm volatile("dc cvac, %0" : : "r"(addr) : "memory");
}

static void dc_civac_line(uintptr_t addr) {
  asm volatile("dc civac, %0" : : "r"(addr) : "memory");
}

static void dc_ivac_line(uintptr_t addr) {
  asm volatile("dc ivac, %0" : : "r"(addr) : "memory");
}

static void dc_range_common(const void* p, size_t len, void (*op)(uintptr_t)) {
  dmb_ish();

  if (len == 0) {
    dsb_ish();
    return;
  }

  size_t line = dcache_line_size();
  if (line == 0) {
    line = 64;
  }

  uintptr_t start = align_down(reinterpret_cast<uintptr_t>(p), static_cast<uintptr_t>(line));
  uintptr_t end = align_up(reinterpret_cast<uintptr_t>(p) + len, static_cast<uintptr_t>(line));

  for (uintptr_t addr = start; addr < end; addr += line) {
    op(addr);
  }

  dsb_ish();
}

}  // namespace

extern "C" void dc_cvac_range(const void* p, size_t len) {
  dc_range_common(p, len, dc_cvac_line);
}

extern "C" void dc_civac_range(const void* p, size_t len) {
  dc_range_common(p, len, dc_civac_line);
}

extern "C" void dc_ivac_range(const void* p, size_t len) {
  dc_range_common(p, len, dc_ivac_line);
}

