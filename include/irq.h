#pragma once
#include "arch/irq.h"

#ifdef __cplusplus
extern "C" {
#endif
void irq_handler_el1(struct irq_frame* frame);
#ifdef __cplusplus
}
#endif
