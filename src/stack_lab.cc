#include "stack_lab.h"

#include <stddef.h>
#include <stdint.h>

#include "arch/cpu_local.h"
#include "drivers/uart_pl011.h"
#include "thread.h"

namespace {
constexpr uintptr_t kGuardPageBytes = 4096u;

constexpr char kHexDigits[] = "0123456789abcdef";

static void puthex64(uint64_t v) {
  if (!v) { uart_putc('0'); return; }
  char b[16]; int i = 0;
  while (v && i < 16) { b[i++] = kHexDigits[v & 0xF]; v >>= 4; }
  while (i--) uart_putc(b[i]);
}

static __attribute__((noinline)) void consume_stack(unsigned depth, unsigned limit) {
  volatile uint8_t buf[256];
  buf[0] = static_cast<uint8_t>(depth);
  asm volatile("" ::: "memory");
  if (depth < limit) {
    consume_stack(depth + 1u, limit);
  }
  asm volatile("" ::: "memory");
}

static void probe_thread(void*) {
  uart_puts("[stack-lab] probe start\n");

  auto* cpu = cpu_local();
  Thread* cur = cpu ? cpu->current_thread : nullptr;
  if (!cur) {
    uart_puts("[stack-lab] no current thread?\n");
    while (1) { asm volatile("wfe"); }
  }

  uart_puts("[stack-lab] stack_base=0x"); puthex64(reinterpret_cast<uintptr_t>(cur->stack_base));
  uart_puts(" stack_size="); uart_print_u64(static_cast<unsigned long long>(cur->stack_size));
  uart_puts("\n");

  // Use a small, known amount of stack and then report the watermark.
  consume_stack(0u, 8u);
  size_t used = thread_stack_high_watermark_bytes(cur);
  uart_puts("[stack-lab] high_watermark_bytes=");
  uart_print_u64(static_cast<unsigned long long>(used));
  uart_puts("\n");

  uintptr_t guard = reinterpret_cast<uintptr_t>(cur->stack_base) - kGuardPageBytes;
  uart_puts("[stack-lab] guard_page=0x"); puthex64(guard); uart_puts("\n");
  uart_puts("[stack-lab] writing guard page (expected [EXC] DABT)\n");

  *reinterpret_cast<volatile uint8_t*>(guard) = 0x42;

  uart_puts("[stack-lab] BUG: guard page write succeeded\n");
  while (1) { asm volatile("wfe"); }
}
}  // namespace

extern "C" void stack_lab_setup(unsigned mode) {
  uart_puts("[stack-lab] setup\n");
  if (mode != 1u) {
    uart_puts("[stack-lab] unknown mode\n");
    while (1) { asm volatile("wfe"); }
  }

  Thread* t = thread_create(probe_thread, nullptr, 8 * 1024);
  if (!t) {
    uart_puts("[stack-lab] thread_create failed\n");
    while (1) { asm volatile("wfe"); }
  }
  sched_add(t);
}

