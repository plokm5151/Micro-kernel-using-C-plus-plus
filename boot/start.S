  .section .text.boot, "ax"
  .global _start
  .extern kmain
  .extern __vec_base
  .extern __bss_start
  .extern __bss_end
_start:
  ldr x0, =__boot_stack_top
  mov sp, x0

  adr x1, __vec_base
  msr VBAR_EL1, x1
  isb

  ldr x1, =__bss_start
  ldr x2, =__bss_end
  cmp x1, x2
  b.hs 2f
1:
  str xzr, [x1], #8
  cmp x1, x2
  b.lo 1b
2:

  // ---- Enable FP/ASIMD at EL1: CPACR_EL1.FPEN[21:20] = 0b11 ----
  mrs x3, cpacr_el1
  orr x3, x3, #(3 << 20)
  msr cpacr_el1, x3
  isb
  // --------------------------------------------------------------

  bl kmain
3: wfe
   b 3b
