  .section .text.boot, "ax"
  .global _start
  .extern kmain
  .extern __vec_base
_start:
  ldr x0, =__boot_stack_top
  mov sp, x0

  adr x1, __vec_base
  msr VBAR_EL1, x1
  isb

  bl kmain
1: wfe
   b 1b
