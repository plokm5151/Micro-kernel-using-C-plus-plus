  .align 11
  .global __vec_base
__vec_base:

// EL1t
  .balign 0x80
  b .

  .balign 0x80
  b irq_el1

  .balign 0x80
  b .

  .balign 0x80
  b .

// EL1h
  .balign 0x80
  b .

  .balign 0x80
  b irq_el1

  .balign 0x80
  b .

  .balign 0x80
  b .

// EL0_64
  .balign 0x80
  b .

  .balign 0x80
  b irq_el1

  .balign 0x80
  b .

  .balign 0x80
  b .

  .equ IRQ_FRAME_SIZE, 192
  .equ IRQ_FRAME_X0, 0
  .equ IRQ_FRAME_X2, 16
  .equ IRQ_FRAME_X4, 32
  .equ IRQ_FRAME_X6, 48
  .equ IRQ_FRAME_X8, 64
  .equ IRQ_FRAME_X10, 80
  .equ IRQ_FRAME_X12, 96
  .equ IRQ_FRAME_X14, 112
  .equ IRQ_FRAME_X16, 128
  .equ IRQ_FRAME_X18, 144
  .equ IRQ_FRAME_LR, 152
  .equ IRQ_FRAME_SP, 160
  .equ IRQ_FRAME_SPSR, 168
  .equ IRQ_FRAME_ELR, 176

  .global irq_el1
irq_el1:
  msr daifset, #0b0010
  sub sp, sp, #IRQ_FRAME_SIZE
  stp x0, x1, [sp, #IRQ_FRAME_X0]
  stp x2, x3, [sp, #IRQ_FRAME_X2]
  stp x4, x5, [sp, #IRQ_FRAME_X4]
  stp x6, x7, [sp, #IRQ_FRAME_X6]
  stp x8, x9, [sp, #IRQ_FRAME_X8]
  stp x10, x11, [sp, #IRQ_FRAME_X10]
  stp x12, x13, [sp, #IRQ_FRAME_X12]
  stp x14, x15, [sp, #IRQ_FRAME_X14]
  stp x16, x17, [sp, #IRQ_FRAME_X16]
  str x18, [sp, #IRQ_FRAME_X18]
  str x30, [sp, #IRQ_FRAME_LR]
  add x18, sp, #IRQ_FRAME_SIZE
  str x18, [sp, #IRQ_FRAME_SP]
  mrs x18, spsr_el1
  str x18, [sp, #IRQ_FRAME_SPSR]
  mrs x18, elr_el1
  str x18, [sp, #IRQ_FRAME_ELR]
  // TODO: spill q0–q7 once SIMD is enabled.
  // irq_handler_el1 executes with IRQs masked.
  mov x0, sp
  bl irq_handler_el1
  mov x9, sp
  ldr x16, [x9, #IRQ_FRAME_SP]
  ldr x17, [x9, #IRQ_FRAME_SPSR]
  ldr x18, [x9, #IRQ_FRAME_ELR]
  msr elr_el1, x18
  msr spsr_el1, x17
  mov sp, x16
  ldp x0, x1, [x9, #IRQ_FRAME_X0]
  ldp x2, x3, [x9, #IRQ_FRAME_X2]
  ldp x4, x5, [x9, #IRQ_FRAME_X4]
  ldp x6, x7, [x9, #IRQ_FRAME_X6]
  ldp x10, x11, [x9, #IRQ_FRAME_X10]
  ldp x12, x13, [x9, #IRQ_FRAME_X12]
  ldp x14, x15, [x9, #IRQ_FRAME_X14]
  ldp x16, x17, [x9, #IRQ_FRAME_X16]
  ldr x18, [x9, #IRQ_FRAME_X18]
  ldr x30, [x9, #IRQ_FRAME_LR]
  ldp x8, x9, [x9, #IRQ_FRAME_X8]
  eret
