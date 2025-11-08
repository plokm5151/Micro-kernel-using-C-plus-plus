    .text
    .align  2
    .global arch_switch
    .type   arch_switch, %function
arch_switch:
    stp x19, x20, [sp, #-16]!
    stp x21, x22, [sp, #-16]!
    stp x23, x24, [sp, #-16]!
    stp x25, x26, [sp, #-16]!
    stp x27, x28, [sp, #-16]!
    stp x29, x30, [sp, #-16]!
    mov x2, sp
    str x2, [x0]
    mov sp, x1
    ldp x29, x30, [sp], #16
    ldp x27, x28, [sp], #16
    ldp x25, x26, [sp], #16
    ldp x23, x24, [sp], #16
    ldp x21, x22, [sp], #16
    ldp x19, x20, [sp], #16
    ret
    .size arch_switch, .-arch_switch

    .align  2
    .global thread_trampoline
    .type   thread_trampoline, %function
    .extern thread_exit
thread_trampoline:
    mrs x0, tpidr_el1
    ldr x1, [x0, #8]      // cpu_local()->current_thread
    ldr x2, [x1, #8]      // Thread::entry
    ldr x3, [x1, #16]     // Thread::arg
    mov x0, x3
    blr x2
    bl thread_exit
1:  wfe
    b 1b
    .size thread_trampoline, .-thread_trampoline
