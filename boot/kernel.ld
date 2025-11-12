ENTRY(_start)
SECTIONS {
  . = 0x40080000;
  .text : { *(.text.boot) *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  . = ALIGN(8);
  .bss : {
    __bss_start = .;
    *(.bss*) *(COMMON)
    . = ALIGN(8);
    __bss_end = .;
  }

  . = ALIGN(16);
  . += 0x4000;      /* 16KB boot stack */
  __boot_stack_top = .;

  . = ALIGN(64);
  __irq_stack_cpu0 = .;
  . += 0x4000;      /* 16KB per-CPU IRQ stack for CPU0 */
  __irq_stack_cpu0_end = .;
  __irq_stack_cpu0_top = __irq_stack_cpu0_end;

  . = ALIGN(4096);
  _heap_start = .;
  . += 0x100000;    /* 1MB kernel heap */
  _heap_end = .;

  . = ALIGN(4096);
  __dma_nc_start = .;
  . += 0x20000;     /* 128KB non-cacheable DMA window */
  __dma_nc_end = .;
}
