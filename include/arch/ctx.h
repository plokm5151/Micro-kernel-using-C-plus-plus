#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// Swap thread stack pointers: stores old sp into *prev_sp and switches to next_sp.
void arch_switch(void** prev_sp, void* next_sp);
// Entry thunk for the first switch into a new thread: reads cpu_local()->current_thread,
// calls entry(arg), and calls thread_exit() when it returns.
void thread_trampoline(void);
#ifdef __cplusplus
}
#endif
