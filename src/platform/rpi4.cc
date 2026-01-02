#include "platform.h"

#include <stdint.h>

#include "drivers/uart_pl011.h"

namespace {
constexpr uintptr_t kPeriphBase = 0xFE000000ull;
constexpr uintptr_t kGpioBase = kPeriphBase + 0x200000u;

constexpr uintptr_t kGpfsel1 = kGpioBase + 0x04u;
constexpr uintptr_t kGppupdn0 = kGpioBase + 0xE4u;

constexpr uintptr_t kUart0Base = kPeriphBase + 0x201000u;

#if defined(RPI4_UART_CLOCK_HZ)
constexpr uint32_t kUart0ClockHz = static_cast<uint32_t>(RPI4_UART_CLOCK_HZ);
#else
constexpr uint32_t kUart0ClockHz = 48000000u;
#endif

static inline void mmio_write32(uintptr_t addr, uint32_t v) {
  *reinterpret_cast<volatile uint32_t*>(addr) = v;
}

static inline uint32_t mmio_read32(uintptr_t addr) {
  return *reinterpret_cast<volatile uint32_t*>(addr);
}

static void gpio_init_pl011_uart0(void) {
  // GPIO14/15 -> ALT0 (PL011 TXD0/RXD0).
  uint32_t fsel1 = mmio_read32(kGpfsel1);
  fsel1 &= ~((7u << 12) | (7u << 15));
  fsel1 |= (4u << 12) | (4u << 15);
  mmio_write32(kGpfsel1, fsel1);

  // BCM2711 uses GPPUPPDN registers: 00 = no pull.
  uint32_t pupdn0 = mmio_read32(kGppupdn0);
  pupdn0 &= ~((3u << 28) | (3u << 30));
  mmio_write32(kGppupdn0, pupdn0);
}
}  // namespace

extern "C" const char* platform_name(void) {
  return "rpi4";
}

extern "C" void platform_early_init(void) {
  // NOTE: RPi4 UART clocks can vary based on firmware config; see docs.
  uart_pl011_set_mmio_base(kUart0Base);
  uart_pl011_set_clock_hz(kUart0ClockHz);
  gpio_init_pl011_uart0();
}

extern "C" void platform_irq_init(void) {
  // TODO: BCM2711 interrupt controller (RPi4) bring-up.
}

extern "C" int platform_full_kernel_supported(void) {
  return 0;
}
