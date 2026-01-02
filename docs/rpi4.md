# Raspberry Pi 4 bring-up (AArch64)

This repo primarily targets QEMU `-machine virt`, but also contains an initial
Raspberry Pi 4 (BCM2711) bring-up path.

## Build artifacts

- `make rpi4` produces:
  - `build/kernel8.img` (raw image for the Pi firmware)
  - `build/kernel_rpi4.elf` (debuggable ELF)

## Hardware checklist

- Raspberry Pi 4 Model B
- microSD card + reader
- USB-to-TTL serial adapter (3.3V logic; **do not use 5V**)
- Jumper wires

## UART wiring (GPIO14/15)

- Pi GPIO14 (TXD0) -> USB-TTL RX
- Pi GPIO15 (RXD0) -> USB-TTL TX
- Pi GND -> USB-TTL GND

Serial settings: `115200 8N1`.

## SD card layout

On the boot FAT partition:

- `config.txt`
- `kernel8.img` (from `build/kernel8.img`)

## Minimal `config.txt`

This uses PL011 UART0 on GPIO14/15 by disabling Bluetooth.

```ini
arm_64bit=1
enable_uart=1
dtoverlay=disable-bt
```

If you see garbled output, the UART clock assumption may not match your
firmware configuration. You can either adjust the firmware clock (advanced), or
rebuild with a different clock value:

```sh
make rpi4 RPI4_UART_CLOCK_HZ=48000000
```

## First expected output

The kernel should print at least:

- `[BOOT] UART ready`

The RPi4 boot stub also attempts to print a single character before and after
the EL2→EL1 transition (`2` then `1`). These are best-effort and may be silent
depending on firmware GPIO/UART setup.

## macOS serial console

Find the device:

```sh
ls /dev/tty.usbserial*
```

Connect:

```sh
screen /dev/tty.usbserial-XXXX 115200
```

Exit `screen`: `Ctrl-A` then `k`.

## Current limitations

- The RPi4 build halts after early init because the BCM2711 interrupt
  controller is not wired up yet (`platform_full_kernel_supported()==0`).
- QEMU virt remains the fully-supported platform for IRQ/timer/scheduler/DMA CI.

## Troubleshooting

- No output:
  - Double-check wiring (TX/RX swapped is common).
  - Ensure `dtoverlay=disable-bt` is present (otherwise PL011 may be used by BT).
  - Confirm you are using 3.3V TTL (not RS-232 levels).
- Garbled output:
  - Confirm `115200 8N1`.
  - Rebuild with `make rpi4 RPI4_UART_CLOCK_HZ=...`.

