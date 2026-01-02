  .section .text.boot, "ax"
  .global _start
  .extern kmain
  .extern __vec_base
  .extern __bss_start
  .extern __bss_end

  // Raspberry Pi 4 (BCM2711) PL011 UART0 base (CPU physical address).
  .equ RPI4_UART0_BASE, 0xFE201000

// Best-effort early UART putc. Requires firmware/GPIO to route UART0 to pins.
pl011_putc_early:
 ldr x1, =RPI4_UART0_BASE
1:
  ldr w2, [x1, #0x18]          // UARTFR
  tst w2, #(1 << 5)            // TXFF
  b.ne 1b
  str w0, [x1, #0x00]          // UARTDR
  ret

_start:
  // Park secondary cores. (Pi firmware may start multiple cores.)
  mrs x0, mpidr_el1
  and x0, x0, #0xFF
  cbz x0, 1f
0:
  wfe
  b 0b
1:
  // Set an initial stack for the current EL.
  ldr x0, =__boot_stack_top
  mov sp, x0

  // Detect current EL (bits [3:2]).
  mrs x0, CurrentEL
  lsr x0, x0, #2
  and x0, x0, #3

  // If we're already in EL1, skip the transition.
  cmp x0, #1
  b.eq el1_entry

  // If we're in EL2, drop to EL1.
  cmp x0, #2
  b.ne .

  // Marker: entering EL2->EL1 transition.
  mov w0, #'2'
  bl pl011_putc_early

  // Configure EL2 so EL1 runs AArch64.
  mov x0, #(1 << 31)           // HCR_EL2.RW=1
  msr hcr_el2, x0

  // Allow EL1 access to timers/counters; keep CNTV offset at 0.
  mov x0, #3                   // CNTHCTL_EL2.EL1PCTEN|EL1PCEN
  msr cnthctl_el2, x0
  msr cntvoff_el2, xzr

  // Disable FP traps from EL1.
  msr cptr_el2, xzr

  // Set EL1 stack pointer for after eret.
  ldr x0, =__boot_stack_top
  msr sp_el1, x0

  // Return to EL1h with interrupts masked.
  mov x0, #(0x3C0 | 0x5)        // DAIF=1111b, M=EL1h
  msr spsr_el2, x0
  adr x0, el1_entry
  msr elr_el2, x0
  isb
  eret

el1_entry:
  // Marker: now executing in EL1.
  mov w0, #'1'
  bl pl011_putc_early

  // Install vector base.
  adr x1, __vec_base
  msr VBAR_EL1, x1
  isb

  // Clear .bss.
  ldr x1, =__bss_start
  ldr x2, =__bss_end
  cmp x1, x2
  b.hs 2f
1:
  str xzr, [x1], #8
  cmp x1, x2
  b.lo 1b
2:

  // Enable FP/ASIMD at EL1/EL0.
  mrs x3, cpacr_el1
  orr x3, x3, #(0b11 << 20)
  msr cpacr_el1, x3
  isb

  bl kmain

3:
  wfe
  b 3b
