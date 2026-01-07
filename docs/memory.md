# Memory labs: pools, fragmentation, stack vs heap

This repo intentionally runs with MMU + I/D caches enabled early. That makes
correctness issues (DMA coherency, stack overflow, allocator behavior) show up
in a realistic way, even on QEMU.

## Why embedded systems often avoid `malloc/free`

General-purpose `malloc/free` is optimized for average-case flexibility, not
real-time predictability. In firmware, the common issues are:

- **Unpredictable latency**: allocation/free can become O(n) as the free list
  fragments (scan + split + coalesce).
- **External fragmentation**: an allocation can fail even when total free memory
  is large, because no single contiguous block is large enough.
- **Overhead**: per-allocation metadata + alignment padding consumes RAM.
- **Harder worst-case reasoning**: long-lived systems accumulate allocator state
  that is difficult to bound and test exhaustively.

## Fixed-size memory pool (`slab`/freelist style)

For many kernel/driver objects (descriptors, small structs, fixed message
buffers), a **fixed-size pool** is often a better fit:

- O(1) allocate/free (pop/push a freelist)
- no external fragmentation
- predictable failure mode (pool exhausted)
- easy to instrument

Implementation: `include/mem_pool.h`, `src/mem_pool.cc`.

In addition to the standalone lab, the kernel also uses a fixed-size pool for
real kernel objects:

- **Thread objects** are allocated from a `mem_pool` initialized in `sched_init`
  (see `src/thread.cc`). This makes the “embedded likes pools” argument
  concrete: predictable allocation with no external fragmentation.

## Internal vs external fragmentation (demo)

This repo includes a deterministic lab that prints **quantifiable evidence** for
allocator tradeoffs:

- **Internal fragmentation**: wasted bytes *inside* an allocation because the
  allocator rounds up (e.g., fixed-size blocks or alignment).
- **External fragmentation**: free memory exists, but is split into many small
  holes so a large contiguous allocation fails.

- **Internal fragmentation demo**: allocate variable request sizes out of a
  fixed-size pool and report wasted bytes.
- **External fragmentation demo**: a tiny first-fit allocator shows how a large
  allocation can fail with enough total free memory, and reports the number of
  search steps (a proxy for latency variability).

Run it by building with `MEM_LAB_MODE=1` (it runs and then halts instead of
starting the scheduler):

```sh
make -j MEM_LAB_MODE=1
make run
```

Expected log contains sections like:

- `[mem-lab][pool] internal_frag_pct=...`
- `[mem-lab][malloc] external_frag_pct=...`
- `[mem-lab][malloc] big alloc FAILED (fragmentation)`

## Stack vs heap in this kernel

This kernel uses several distinct memory regions (see `boot/kernel.ld`):

- **Boot stack**: a fixed 16 KiB stack used only during early boot (`boot/start.S`).
- **IRQ stack**: a fixed 16 KiB per-CPU stack used by the EL1 IRQ entry stub
  (`boot/vectors.S`) via `cpu_local()->irq_stack_top`.
- **Thread stacks**: each kernel thread gets its own stack allocated from the
  early heap (`thread_create()`).
- **Heap**: a simple 1 MiB bump allocator (`kmem_alloc_aligned`), suitable for
  early boot and demos.

## Stack guard + watermark

Thread stacks are initialized with:

- a **guard region** at the bottom of the stack (detects overflow past the base)
- a **watermark fill pattern** for the remaining unused area (estimates high-water mark)
- an **MMU guard page** below the stack base (unmapped 4 KiB page; catches
  overflows immediately with a synchronous exception when the MMU is enabled)

The scheduler tick path checks the current thread’s guard on every timer tick
and halts with a diagnostic if it is corrupted.

APIs:

- `thread_stack_guard_ok(t)` returns 1 if the guard is intact.
- `thread_stack_high_watermark_bytes(t)` returns an estimated maximum stack
  usage (bytes) since the thread was created.

### Stack lab (guard page)

To validate that the guard page is really active under MMU+caches, run the stack
lab which intentionally faults by touching the unmapped page below the stack:

```sh
STACK_LAB_MODE=1 scripts/stack_lab_run.sh
```

Expected log contains:

- `[stack-lab] writing guard page ...`
- `[EXC] ... (DABT) ...`
