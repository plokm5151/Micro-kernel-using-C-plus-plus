#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Save/load current thread's FPSIMD context (q0..q31 + FPCR/FPSR).
// No arguments: implementation locates cpu_local()->current_thread.
void fpsimd_save(void);
void fpsimd_load(void);

#ifdef __cplusplus
}
#endif
