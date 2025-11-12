#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct fpsimd_state {
  // 32個 Q 暫存器 × 16 bytes = 512 bytes
  uint8_t  q[32][16];
  uint32_t fpcr;  // FP Control Register
  uint32_t fpsr;  // FP Status Register
} __attribute__((aligned(16)));

void fpsimd_save(struct fpsimd_state* st);
void fpsimd_load(const struct fpsimd_state* st);

#ifdef __cplusplus
}
#endif
