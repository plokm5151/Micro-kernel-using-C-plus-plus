#include <stdint.h>
#include "drivers/uart_pl011.h"
#include "arch/cpu_local.h"
#include "arch/gicv3.h"
#include "arch/irq.h"
#include "arch/timer.h"
#include "preempt.h"
#include "thread.h"
#include "dma.h"

extern "C" void irq_handler_el1(struct irq_frame* frame) {
  uart_putc('!');

  auto* cpu = cpu_local();
  cpu->irq_depth++;

  uint32_t iar = gic_ack();
  uint32_t intid = iar & 0xFFFFFFu;
  bool handled_timer = false;

  if (intid == 27u) {           // virtual timer PPI
    uart_putc(':');
    timer_irq();
    gic_eoi(iar);
    handled_timer = true;
  } else if (intid == 1023u) {
    cpu->irq_depth--;
    return;
  } else {
    uart_putc('?');
    gic_eoi(iar);
  }

  if (handled_timer) {
    cpu->ticks++;
    dma_poll_complete();
    sched_on_tick();
    if (cpu->current_thread && cpu->preempt_cnt == 0 && cpu->need_resched) {
      frame->elr = reinterpret_cast<uint64_t>(&preempt_return);
    }
  }

  cpu->irq_depth--;
}
