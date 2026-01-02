#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform identifier string (e.g. "virt", "rpi4").
const char* platform_name(void);

// Perform platform-specific setup needed before uart_init():
// - set PL011 MMIO base/clock
// - configure GPIO mux (if required)
void platform_early_init(void);

// Initialize the platform interrupt controller (if present).
void platform_irq_init(void);

// Return 1 if the build supports the full IRQ+timer+RR scheduler path.
// RPi4 bring-up starts in a UART-only mode until its IRQ controller support is
// implemented.
int platform_full_kernel_supported(void);

#ifdef __cplusplus
}
#endif

