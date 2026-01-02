#include "platform.h"

#include "arch/gicv3.h"
#include "drivers/uart_pl011.h"

extern "C" const char* platform_name(void) {
  return "virt";
}

extern "C" void platform_early_init(void) {
  // QEMU virt uses a PL011 at 0x0900_0000. The clock is typically 24 MHz.
  uart_pl011_set_mmio_base(0x09000000ull);
  uart_pl011_set_clock_hz(24000000u);
}

extern "C" void platform_irq_init(void) {
  gic_init();
}

extern "C" int platform_full_kernel_supported(void) {
  return 1;
}

