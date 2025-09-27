#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/gicv3.h"
#include "arch/irq.h"
#include "arch/timer.h"

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
  high_arg_sink = touch_high_args(frame->regs[0], frame->regs[1],
                                  frame->regs[2], frame->regs[3],
                                  frame->regs[4], frame->regs[5],
                                  frame->regs[6], frame->regs[7]);

  uint32_t iar = gic_ack();
  uint32_t intid = iar & 0xFFFFFFu;
  if (intid == 27u) {           // virtual timer PPI
    timer_irq();
  } else {
    uart_puts("IRQ?\n");
  }
  gic_eoi(iar);
}
