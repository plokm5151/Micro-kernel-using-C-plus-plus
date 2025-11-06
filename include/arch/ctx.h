#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 交換執行緒堆疊指標：*prev_sp 存回舊 sp，切換到 next_sp
void arch_switch(void** prev_sp, void* next_sp);
// 首次切入新執行緒用的 thunk：讀取 cpu_local()->current_thread，
// 呼叫其 entry(arg)，返回時呼叫 thread_exit() 結束。
void thread_trampoline(void);
#ifdef __cplusplus
}
#endif
