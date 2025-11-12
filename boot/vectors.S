  .align 11
  .global __vec_base
__vec_base:

// ===== EL1t =====
  .balign 0x80                // Synchronous EL1t
  b sync_el1t
  .balign 0x80                // IRQ EL1t
  b irq_el1
  .balign 0x80                // FIQ EL1t (unused)
  b sync_el1t
  .balign 0x80                // SError EL1t
  b sync_el1t

// ===== EL1h =====
  .balign 0x80                // Synchronous EL1h
  b sync_el1h
  .balign 0x80                // IRQ EL1h
  b irq_el1
  .balign 0x80                // FIQ EL1h (unused)
  b sync_el1h
  .balign 0x80                // SError EL1h
  b sync_el1h

// ===== EL0_64 =====
  .balign 0x80                // Synchronous EL0_64
  b sync_el0_64
  .balign 0x80                // IRQ EL0_64
  b irq_el1
  .balign 0x80                // FIQ EL0_64 (unused)
  b sync_el0_64
  .balign 0x80                // SError EL0_64
  b sync_el0_64

  // --- IRQ frame layout 與 irq_el1 保持原樣 ---
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
  mrs x19, tpidr_el1
  cbz x19, 1f
  ldr x20, [x19, #0]
  mov x21, sp
  mov sp, x20
  sub sp, sp, #IRQ_FRAME_SIZE
  b 2f
1:
  sub sp, sp, #IRQ_FRAME_SIZE
  add x21, sp, #IRQ_FRAME_SIZE
2:
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
  str x21, [sp, #IRQ_FRAME_SP]
  mrs x18, spsr_el1
  str x18, [sp, #IRQ_FRAME_SPSR]
  mrs x18, elr_el1
  str x18, [sp, #IRQ_FRAME_ELR]
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

  .extern except_el1_sync

// --- 極簡同步例外轉印到 C ---
sync_el1h:
  mrs x0, esr_el1
  mrs x1, elr_el1
  mrs x2, far_el1
  mrs x3, spsr_el1
  mov x4, #0x11    // tag: EL1h
  bl except_el1_sync
  b .

sync_el1t:
  mrs x0, esr_el1
  mrs x1, elr_el1
  mrs x2, far_el1
  mrs x3, spsr_el1
  mov x4, #0x10    // tag: EL1t
  bl except_el1_sync
  b .

sync_el0_64:
  mrs x0, esr_el1
  mrs x1, elr_el1
  mrs x2, far_el1
  mrs x3, spsr_el1
  mov x4, #0x0     // tag: EL0_64
  bl except_el1_sync
  b .
