#include "dma_lab.h"

#include <stddef.h>
#include <stdint.h>
#include "arch/barrier.h"
#include "dma.h"
#include "drivers/uart_pl011.h"
#include "kmem.h"

namespace {
constexpr uintptr_t kNormalStart = 0x40000000ull;
constexpr uintptr_t kNormalEnd = 0x80000000ull;  // exclusive
constexpr uintptr_t kAliasOffset = 0x40000000ull;

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

static void uart_puthex64(uint64_t value) {
  constexpr char kHexDigits[] = "0123456789abcdef";
  if (value == 0) { uart_putc('0'); return; }
  char buf[16]; int idx = 0;
  while (value && idx < 16) { buf[idx++] = kHexDigits[value & 0xFu]; value >>= 4; }
  while (idx--) uart_putc(buf[idx]);
}

static inline void* to_alias(void* p, size_t len) {
  if (!p || len == 0) return nullptr;
  uintptr_t start = reinterpret_cast<uintptr_t>(p);
  uintptr_t end = start + (len - 1u);
  if (end < start) return nullptr;
  if (start < kNormalStart || end >= kNormalEnd) return nullptr;
  return reinterpret_cast<void*>(start + kAliasOffset);
}

static void memfill(uint8_t* p, size_t len, uint8_t seed) {
  for (size_t i = 0; i < len; ++i) {
    p[i] = static_cast<uint8_t>(seed + static_cast<uint8_t>(i * 7u));
  }
}

static void memfill_const(uint8_t* p, size_t len, uint8_t v) {
  for (size_t i = 0; i < len; ++i) {
    p[i] = v;
  }
}

static int first_mismatch(const uint8_t* a, const uint8_t* b, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (a[i] != b[i]) return static_cast<int>(i);
  }
  return -1;
}

static void device_memcpy(void* dst, const void* src, size_t len) {
  auto* d = static_cast<uint8_t*>(dst);
  auto* s = static_cast<const uint8_t*>(src);
  for (size_t i = 0; i < len; ++i) {
    d[i] = s[i];
  }
}

static void clean_to_poc(void* p, size_t len) {
  dc_cvac_range(p, len);
  dma_wmb();
}

static void invalidate_for_cpu(void* p, size_t len) {
  dma_rmb();
  dc_ivac_range(p, len);
}

static bool case1_cpu_to_dev_stale_src() {
  uart_puts("[dma-lab][1] stale read CPU->device (missing clean)\n");
  uart_puts("[dma-lab][1] why: device reads via NC alias, CPU dirty cache not visible\n");

  constexpr size_t kLen = 1024;
  auto* src = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* dst = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* src_alias = static_cast<const uint8_t*>(to_alias(src, kLen));
  auto* dst_alias = static_cast<uint8_t*>(to_alias(dst, kLen));
  if (!src || !dst || !src_alias || !dst_alias) return false;

  // Ensure memory starts with an "old" pattern and caches are not carrying it.
  memfill_const(src, kLen, 0x11);
  dc_cvac_range(src, kLen);
  dc_ivac_range(src, kLen);

  memfill_const(dst, kLen, 0x00);
  dc_cvac_range(dst, kLen);
  dc_ivac_range(dst, kLen);

  // CPU writes "new" pattern, but does NOT clean.
  memfill(src, kLen, 0x80);

  // Device copies using NC alias view.
  device_memcpy(dst_alias, src_alias, kLen);

  // CPU does correct completion for reading device-written dst.
  invalidate_for_cpu(dst, kLen);

  // Expect mismatch (device saw old memory).
  uint8_t expected[kLen];
  memfill(expected, kLen, 0x80);
  int mismatch = first_mismatch(dst, expected, kLen);
  if (mismatch < 0) {
    uart_puts("[dma-lab][1] ERR unexpected PASS (bug did not reproduce)\n");
    return false;
  }
  uart_puts("[dma-lab][1] OK expected FAIL without clean: mismatch@");
  uart_print_u64(static_cast<unsigned long long>(mismatch));
  uart_puts("\n");

  // Fix: clean + wmb before device reads.
  clean_to_poc(src, kLen);
  device_memcpy(dst_alias, src_alias, kLen);
  invalidate_for_cpu(dst, kLen);

  mismatch = first_mismatch(dst, expected, kLen);
  if (mismatch >= 0) {
    uart_puts("[dma-lab][1] ERR fix failed: still mismatching@");
    uart_print_u64(static_cast<unsigned long long>(mismatch));
    uart_puts("\n");
    return false;
  }
  uart_puts("[dma-lab][1] OK fixed PASS (clean + dma_wmb)\n");
  return true;
}

static bool case2_dev_to_cpu_stale_dst() {
  uart_puts("[dma-lab][2] stale read device->CPU (missing invalidate)\n");
  uart_puts("[dma-lab][2] why: device writes memory via NC alias, CPU may keep stale cache line\n");

  constexpr size_t kLen = 1024;
  auto* dst = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* dst_alias = static_cast<uint8_t*>(to_alias(dst, kLen));
  if (!dst || !dst_alias) return false;

  // CPU caches old value.
  memfill_const(dst, kLen, 0xAA);
  dc_cvac_range(dst, kLen);     // make memory old=0xAA
  (void)dst[0];                 // touch to keep in cache

  // Device writes new value to memory via alias.
  memfill_const(dst_alias, kLen, 0x55);

  // Bug: CPU reads without invalidation -> should still see old 0xAA.
  bool saw_new = false;
  for (size_t i = 0; i < kLen; ++i) {
    if (dst[i] != 0xAA) { saw_new = true; break; }
  }
  if (saw_new) {
    uart_puts("[dma-lab][2] ERR unexpected PASS (CPU observed device write without invalidate)\n");
    return false;
  }
  uart_puts("[dma-lab][2] OK expected FAIL without invalidate (CPU kept stale cache)\n");

  // Fix: invalidate + rmb.
  invalidate_for_cpu(dst, kLen);
  for (size_t i = 0; i < kLen; ++i) {
    if (dst[i] != 0x55) {
      uart_puts("[dma-lab][2] ERR fix failed: dst mismatch@");
      uart_print_u64(static_cast<unsigned long long>(i));
      uart_puts(" val=0x"); uart_puthex64(dst[i]); uart_puts("\n");
      return false;
    }
  }
  uart_puts("[dma-lab][2] OK fixed PASS (dma_rmb + invalidate)\n");
  return true;
}

struct LabDesc {
  uint64_t src;
  uint64_t dst;
  uint64_t len;
  uint64_t cookie;
};

static bool device_process_desc(const LabDesc* desc_alias) {
  if (!desc_alias) return false;
  uint64_t len = desc_alias->len;
  uint64_t src = desc_alias->src;
  uint64_t dst = desc_alias->dst;
  if (len == 0 || len > 4096) return false;
  device_memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(dst)),
                reinterpret_cast<const void*>(static_cast<uintptr_t>(src)),
                static_cast<size_t>(len));
  return true;
}

static bool case3_descriptor_stale_fields() {
  uart_puts("[dma-lab][3] descriptor stale fields (missing clean)\n");
  uart_puts("[dma-lab][3] why: device reads descriptor via NC alias; CPU must clean before publish\n");

  constexpr size_t kLen = 512;
  auto* src = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* dst = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* desc = static_cast<LabDesc*>(kmem_alloc_aligned(sizeof(LabDesc), 64));
  if (!src || !dst || !desc) return false;

  auto* src_alias = reinterpret_cast<uint8_t*>(to_alias(src, kLen));
  auto* dst_alias = reinterpret_cast<uint8_t*>(to_alias(dst, kLen));
  auto* desc_alias = reinterpret_cast<const LabDesc*>(to_alias(desc, sizeof(LabDesc)));
  if (!src_alias || !dst_alias || !desc_alias) return false;

  memfill(src, kLen, 0x10);
  clean_to_poc(src, kLen);

  memfill_const(dst, kLen, 0x00);
  clean_to_poc(dst, kLen);

  // Make sure descriptor memory is zeros in memory.
  memfill_const(reinterpret_cast<uint8_t*>(desc), sizeof(LabDesc), 0x00);
  dc_cvac_range(desc, sizeof(LabDesc));
  dc_ivac_range(desc, sizeof(LabDesc));

  // CPU writes descriptor fields, but does NOT clean.
  desc->src = reinterpret_cast<uint64_t>(src_alias);
  desc->dst = reinterpret_cast<uint64_t>(dst_alias);
  desc->len = kLen;
  desc->cookie = 0x1234;

  bool ok = device_process_desc(desc_alias);
  invalidate_for_cpu(dst, kLen);

  uint8_t expected[kLen];
  memfill(expected, kLen, 0x10);
  int mismatch = first_mismatch(dst, expected, kLen);
  if (ok || mismatch < 0) {
    uart_puts("[dma-lab][3] ERR unexpected PASS (descriptor bug did not reproduce)\n");
    return false;
  }
  uart_puts("[dma-lab][3] OK expected FAIL without descriptor clean (device saw len=0)\n");

  // Fix: clean descriptor then publish.
  clean_to_poc(desc, sizeof(LabDesc));
  ok = device_process_desc(desc_alias);
  invalidate_for_cpu(dst, kLen);
  mismatch = first_mismatch(dst, expected, kLen);
  if (!ok || mismatch >= 0) {
    uart_puts("[dma-lab][3] ERR fix failed (clean + dma_wmb)\n");
    return false;
  }
  uart_puts("[dma-lab][3] OK fixed PASS (clean desc + dma_wmb)\n");
  return true;
}

static bool case4_publish_ordering_head_vs_desc() {
  uart_puts("[dma-lab][4] publish ordering (head visible before desc)\n");
  uart_puts("[dma-lab][4] why: must order desc clean before doorbell/head publish (dma_wmb)\n");

  constexpr size_t kLen = 256;
  auto* src = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* dst = static_cast<uint8_t*>(kmem_alloc_aligned(kLen, 64));
  auto* desc = static_cast<LabDesc*>(kmem_alloc_aligned(sizeof(LabDesc), 64));
  auto* head = static_cast<uint64_t*>(kmem_alloc_aligned(sizeof(uint64_t), 64));
  if (!src || !dst || !desc || !head) return false;

  auto* src_alias = reinterpret_cast<uint8_t*>(to_alias(src, kLen));
  auto* dst_alias = reinterpret_cast<uint8_t*>(to_alias(dst, kLen));
  auto* desc_alias = reinterpret_cast<const LabDesc*>(to_alias(desc, sizeof(LabDesc)));
  auto* head_alias = reinterpret_cast<const volatile uint64_t*>(to_alias(head, sizeof(uint64_t)));
  if (!src_alias || !dst_alias || !desc_alias || !head_alias) return false;

  memfill(src, kLen, 0x33);
  clean_to_poc(src, kLen);
  memfill_const(dst, kLen, 0x00);
  clean_to_poc(dst, kLen);

  // Initialize queue head to 0 in memory.
  *head = 0;
  clean_to_poc(head, sizeof(uint64_t));

  // Ensure descriptor memory is zeros in memory.
  memfill_const(reinterpret_cast<uint8_t*>(desc), sizeof(LabDesc), 0x00);
  dc_cvac_range(desc, sizeof(LabDesc));
  dc_ivac_range(desc, sizeof(LabDesc));

  // CPU writes descriptor, but does NOT clean it.
  desc->src = reinterpret_cast<uint64_t>(src_alias);
  desc->dst = reinterpret_cast<uint64_t>(dst_alias);
  desc->len = kLen;
  desc->cookie = 0xfeed;

  // Publish head (cleaned) pointing to descriptor alias, without cleaning desc.
  *head = reinterpret_cast<uint64_t>(desc_alias);
  dc_cvac_range(head, sizeof(uint64_t));  // head visible

  // Device observes head and tries to process descriptor.
  uint64_t observed = *head_alias;
  bool ok = device_process_desc(reinterpret_cast<const LabDesc*>(static_cast<uintptr_t>(observed)));
  invalidate_for_cpu(dst, kLen);

  uint8_t expected[kLen];
  memfill(expected, kLen, 0x33);
  int mismatch = first_mismatch(dst, expected, kLen);
  if (ok || mismatch < 0) {
    uart_puts("[dma-lab][4] ERR unexpected PASS (publish ordering bug did not reproduce)\n");
    return false;
  }
  uart_puts("[dma-lab][4] OK expected FAIL (head published but desc not cleaned)\n");

  // Fix: clean desc, dma_wmb, then clean/publish head.
  clean_to_poc(desc, sizeof(LabDesc));
  dma_wmb();
  dc_cvac_range(head, sizeof(uint64_t));

  observed = *head_alias;
  ok = device_process_desc(reinterpret_cast<const LabDesc*>(static_cast<uintptr_t>(observed)));
  invalidate_for_cpu(dst, kLen);
  mismatch = first_mismatch(dst, expected, kLen);
  if (!ok || mismatch >= 0) {
    uart_puts("[dma-lab][4] ERR fix failed (desc clean before publish)\n");
    return false;
  }
  uart_puts("[dma-lab][4] OK fixed PASS (clean desc + dma_wmb before publish)\n");
  return true;
}

static bool case5_completion_flag_race() {
  uart_puts("[dma-lab][5] completion flag race (missing invalidate on poll)\n");
  uart_puts("[dma-lab][5] why: polling cacheable flag needs invalidate/rmb to observe device writes\n");

  auto* flag = static_cast<uint32_t*>(kmem_alloc_aligned(sizeof(uint32_t), 64));
  if (!flag) return false;
  volatile uint32_t* flag_vol = flag;
  auto* flag_alias = static_cast<volatile uint32_t*>(to_alias(flag, sizeof(uint32_t)));
  if (!flag_alias) return false;

  *flag = 0;
  clean_to_poc(flag, sizeof(uint32_t));
  (void)*flag_vol;  // cache it

  // Device completes by writing 1 via alias.
  *flag_alias = 1;

  // Bug: spin reads cacheable flag without invalidation => likely never sees 1.
  bool saw = false;
  for (unsigned i = 0; i < 200000; ++i) {
    if (*flag_vol == 1) { saw = true; break; }
  }
  if (saw) {
    uart_puts("[dma-lab][5] ERR unexpected PASS (saw completion without invalidate)\n");
    return false;
  }
  uart_puts("[dma-lab][5] OK expected FAIL (stuck on stale cached flag)\n");

  // Fix: invalidate flag line then read.
  invalidate_for_cpu(flag, sizeof(uint32_t));
  if (*flag_vol != 1) {
    uart_puts("[dma-lab][5] ERR fix failed: still not seeing completion\n");
    return false;
  }
  uart_puts("[dma-lab][5] OK fixed PASS (dma_rmb + invalidate)\n");
  return true;
}

static bool case6_cacheline_sharing_hazard() {
  uart_puts("[dma-lab][6] cache line sharing hazard (invalidate drops dirty neighbor)\n");
  uart_puts("[dma-lab][6] why: invalidate works on whole lines; DMA buffers must not share lines\n");

  const size_t line = dcache_line_size();
  if (line < 32 || (line & (line - 1u)) != 0) {
    uart_puts("[dma-lab][6] unsupported dcache line size\n");
    return false;
  }

  // --- Bug: misaligned buffer shares a line with unrelated data ---
  auto* base = static_cast<uint8_t*>(kmem_alloc_aligned(line * 2, line));
  if (!base) return false;

  uint8_t* guard = &base[0];
  uint8_t* buf = &base[1];
  size_t len = line - 2u;
  uint8_t* buf_alias = static_cast<uint8_t*>(to_alias(buf, len));
  if (!buf_alias) return false;

  memfill_const(base, line, 0x00);
  clean_to_poc(base, line);
  dc_ivac_range(base, line);

  // Device writes buffer via alias.
  memfill_const(buf_alias, len, 0x5A);

  // CPU modifies guard (dirty in cache) then invalidates buffer range.
  *guard = 0xCC;
  dc_ivac_range(buf, len);  // drops dirty guard because it shares the line

  if (*guard == 0xCC) {
    uart_puts("[dma-lab][6] ERR unexpected PASS (guard survived invalidate)\n");
    return false;
  }
  uart_puts("[dma-lab][6] OK expected FAIL: guard lost due to line invalidate\n");

  // --- Fix: put DMA buffer on a separate cache line ---
  auto* base2 = static_cast<uint8_t*>(kmem_alloc_aligned(line * 2, line));
  if (!base2) return false;
  uint8_t* guard2 = &base2[0];
  uint8_t* buf2 = &base2[line];
  size_t len2 = line;
  uint8_t* buf2_alias = static_cast<uint8_t*>(to_alias(buf2, len2));
  if (!buf2_alias) return false;

  memfill_const(base2, line * 2, 0x00);
  clean_to_poc(base2, line * 2);
  dc_ivac_range(base2, line * 2);

  memfill_const(buf2_alias, len2, 0x7E);
  *guard2 = 0xCC;

  // Invalidate only the buffer line.
  dc_ivac_range(buf2, len2);

  if (*guard2 != 0xCC) {
    uart_puts("[dma-lab][6] ERR fix failed: guard corrupted\n");
    return false;
  }
  for (size_t i = 0; i < len2; ++i) {
    if (buf2[i] != 0x7E) {
      uart_puts("[dma-lab][6] ERR fix failed: buffer mismatch@");
      uart_print_u64(static_cast<unsigned long long>(i));
      uart_puts("\n");
      return false;
    }
  }
  uart_puts("[dma-lab][6] OK fixed PASS (line-aligned buffer avoids sharing)\n");
  return true;
}

static bool run_case(unsigned n) {
  switch (n) {
    case 1: return case1_cpu_to_dev_stale_src();
    case 2: return case2_dev_to_cpu_stale_dst();
    case 3: return case3_descriptor_stale_fields();
    case 4: return case4_publish_ordering_head_vs_desc();
    case 5: return case5_completion_flag_race();
    case 6: return case6_cacheline_sharing_hazard();
    default:
      uart_puts("[dma-lab] unknown case\n");
      return false;
  }
}
}  // namespace

extern "C" void dma_lab_run(unsigned mode) {
  uart_puts("[dma-lab] begin policy=");
  uart_puts(DMA_WINDOW_POLICY_STR);
  uart_puts(" mode=");
  uart_print_u64(static_cast<unsigned long long>(mode));
  uart_puts("\n");

  if (!dcache_enabled()) {
    uart_puts("[dma-lab] dcache disabled; lab requires caches-on\n");
    return;
  }

  const size_t line = dcache_line_size();
  uart_puts("[dma-lab] dcache_line=");
  uart_print_u64(static_cast<unsigned long long>(line));
  uart_puts("\n");

  unsigned pass = 0;
  unsigned total = 0;
  if (mode == 1) {
    for (unsigned c = 1; c <= 6; ++c) {
      ++total;
      if (run_case(c)) {
        ++pass;
      }
    }
  } else {
    total = 1;
    pass = run_case(mode) ? 1u : 0u;
  }

  uart_puts("[dma-lab] result ");
  if (pass == total) {
    uart_puts("PASS (");
    uart_print_u64(static_cast<unsigned long long>(pass));
    uart_puts("/");
    uart_print_u64(static_cast<unsigned long long>(total));
    uart_puts(")\n");
  } else {
    uart_puts("FAIL (");
    uart_print_u64(static_cast<unsigned long long>(pass));
    uart_puts("/");
    uart_print_u64(static_cast<unsigned long long>(total));
    uart_puts(")\n");
  }
}
