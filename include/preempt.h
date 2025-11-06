#pragma once
#ifdef __cplusplus
extern "C" {
#endif
void preempt_disable(void);
void preempt_enable(void);
void preempt_return(void);
#ifdef __cplusplus
}
#endif
