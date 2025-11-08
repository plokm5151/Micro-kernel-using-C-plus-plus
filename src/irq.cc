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
  uint32_t intid = iar & 0x3FFu;

  switch (intid) {
    case 27u:  // virtual timer
    case 30u:  // physical timer
      timer_irq();
      cpu->ticks++;
      sched_on_tick();
      dma_poll_complete();
      if (cpu->current_thread && cpu->preempt_cnt == 0 && cpu->need_resched) {
        frame->elr = reinterpret_cast<uint64_t>(&preempt_return);
      }
      break;
    case 1023u:  // spurious
      cpu->irq_depth--;
      return;
    default:
      if (intid < 16u) {
        uart_putc('^');
      } else {
        uart_puts("[irq] unexpected intid\n");
      }
      break;
  }

  if (intid != 1023u) {
    gic_eoi(iar);
  }

  cpu->irq_depth--;
}
