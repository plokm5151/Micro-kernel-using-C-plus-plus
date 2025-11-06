#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/cpu_local.h"
#include "arch/gicv3.h"
#include "arch/irq.h"
#include "arch/timer.h"
#include "preempt.h"
#include "thread.h"

namespace {

__attribute__((noinline)) uint64_t touch_high_args(uint64_t a0, uint64_t a1,
                                                   uint64_t a2, uint64_t a3,
                                                   uint64_t a4, uint64_t a5,
                                                   uint64_t a6, uint64_t a7) {
  return (a4 ^ a5) + (a6 ^ a7) + (a0 | a1 | a2 | a3);
}

volatile uint64_t high_arg_sink;

}  // namespace

extern "C" void irq_handler_el1(struct irq_frame* frame) {
  static uint32_t irq_counter;
  high_arg_sink = touch_high_args(frame->regs[0], frame->regs[1],
                                  frame->regs[2], frame->regs[3],
                                  frame->regs[4], frame->regs[5],
                                  frame->regs[6], frame->regs[7]);

  auto* cpu = cpu_local();
  cpu->irq_depth++;

  uint32_t iar = gic_ack();
  uint32_t intid = iar & 0xFFFFFFu;
  if (intid == 27u) {           // virtual timer PPI
    timer_irq();
    cpu->ticks++;
    sched_on_tick();
    if (cpu->current_thread && cpu->preempt_cnt == 0 && cpu->need_resched) {
      frame->elr = reinterpret_cast<uint64_t>(&preempt_return);
    }
  } else {
    uart_puts("IRQ?\n");
  }
  gic_eoi(iar);
  cpu->irq_depth--;

  irq_counter++;
  if ((irq_counter & 0xFFu) == 0u) {
    struct cpu_local* local = cpu_local();
    uintptr_t delta = (uintptr_t)local->irq_stack_top - frame->sp;
    uart_puts("[irq] delta=");
    uart_print_u64(delta);
  }
}
