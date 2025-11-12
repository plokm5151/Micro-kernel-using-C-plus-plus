#include "arch/fpsimd.h"
#include "arch/cpu_local.h"
#include "thread.h"

#include <stdint.h>

// 這份實作不做 lazy enable，因為我們已在 boot/start.S 打開 CPACR_EL1.FPEN=0b11。
// 注意：本檔會用到 q0..q31 指令，請勿用 -mgeneral-regs-only 編譯本檔。

extern "C" void fpsimd_save(void) {
  auto* cpu = cpu_local();
  if (!cpu || !cpu->current_thread) return;
  Thread* t = cpu->current_thread;

  // Save FPCR/FPSR
  uint64_t fpcr = 0, fpsr = 0;
  asm volatile("mrs %0, fpcr" : "=r"(fpcr));
  asm volatile("mrs %0, fpsr" : "=r"(fpsr));
  t->fpcr = fpcr;
  t->fpsr = fpsr;

  // Save q0..q31 -> t->fpsimd_vregs[512]
  uint8_t* p = reinterpret_cast<uint8_t*>(t->fpsimd_vregs);
  asm volatile(
      "stp q0,  q1,  [%[p], #0]\n\t"
      "stp q2,  q3,  [%[p], #32]\n\t"
      "stp q4,  q5,  [%[p], #64]\n\t"
      "stp q6,  q7,  [%[p], #96]\n\t"
      "stp q8,  q9,  [%[p], #128]\n\t"
      "stp q10, q11, [%[p], #160]\n\t"
      "stp q12, q13, [%[p], #192]\n\t"
      "stp q14, q15, [%[p], #224]\n\t"
      "stp q16, q17, [%[p], #256]\n\t"
      "stp q18, q19, [%[p], #288]\n\t"
      "stp q20, q21, [%[p], #320]\n\t"
      "stp q22, q23, [%[p], #352]\n\t"
      "stp q24, q25, [%[p], #384]\n\t"
      "stp q26, q27, [%[p], #416]\n\t"
      "stp q28, q29, [%[p], #448]\n\t"
      "stp q30, q31, [%[p], #480]\n\t"
      :
      : [p] "r"(p)
      : "memory");

  t->fpsimd_valid = 1;
}

extern "C" void fpsimd_load(void) {
  auto* cpu = cpu_local();
  if (!cpu || !cpu->current_thread) return;
  Thread* t = cpu->current_thread;

  // 若尚未存過，先清零 q-reg（保險；一般情況 thread 剛建立時為 0）
  if (!t->fpsimd_valid) {
    asm volatile(
        "eor v0.16b,  v0.16b,  v0.16b\n\t"
        "eor v1.16b,  v1.16b,  v1.16b\n\t"
        "eor v2.16b,  v2.16b,  v2.16b\n\t"
        "eor v3.16b,  v3.16b,  v3.16b\n\t"
        "eor v4.16b,  v4.16b,  v4.16b\n\t"
        "eor v5.16b,  v5.16b,  v5.16b\n\t"
        "eor v6.16b,  v6.16b,  v6.16b\n\t"
        "eor v7.16b,  v7.16b,  v7.16b\n\t"
        "eor v8.16b,  v8.16b,  v8.16b\n\t"
        "eor v9.16b,  v9.16b,  v9.16b\n\t"
        "eor v10.16b, v10.16b, v10.16b\n\t"
        "eor v11.16b, v11.16b, v11.16b\n\t"
        "eor v12.16b, v12.16b, v12.16b\n\t"
        "eor v13.16b, v13.16b, v13.16b\n\t"
        "eor v14.16b, v14.16b, v14.16b\n\t"
        "eor v15.16b, v15.16b, v15.16b\n\t"
        "eor v16.16b, v16.16b, v16.16b\n\t"
        "eor v17.16b, v17.16b, v17.16b\n\t"
        "eor v18.16b, v18.16b, v18.16b\n\t"
        "eor v19.16b, v19.16b, v19.16b\n\t"
        "eor v20.16b, v20.16b, v20.16b\n\t"
        "eor v21.16b, v21.16b, v21.16b\n\t"
        "eor v22.16b, v22.16b, v22.16b\n\t"
        "eor v23.16b, v23.16b, v23.16b\n\t"
        "eor v24.16b, v24.16b, v24.16b\n\t"
        "eor v25.16b, v25.16b, v25.16b\n\t"
        "eor v26.16b, v26.16b, v26.16b\n\t"
        "eor v27.16b, v27.16b, v27.16b\n\t"
        "eor v28.16b, v28.16b, v28.16b\n\t"
        "eor v29.16b, v29.16b, v29.16b\n\t"
        "eor v30.16b, v30.16b, v30.16b\n\t"
        "eor v31.16b, v31.16b, v31.16b\n\t"
        :
        :
        : "memory");
  } else {
    // Restore q0..q31 <- t->fpsimd_vregs[512]
    const uint8_t* p = reinterpret_cast<const uint8_t*>(t->fpsimd_vregs);
    asm volatile(
        "ldp q0,  q1,  [%[p], #0]\n\t"
        "ldp q2,  q3,  [%[p], #32]\n\t"
        "ldp q4,  q5,  [%[p], #64]\n\t"
        "ldp q6,  q7,  [%[p], #96]\n\t"
        "ldp q8,  q9,  [%[p], #128]\n\t"
        "ldp q10, q11, [%[p], #160]\n\t"
        "ldp q12, q13, [%[p], #192]\n\t"
        "ldp q14, q15, [%[p], #224]\n\t"
        "ldp q16, q17, [%[p], #256]\n\t"
        "ldp q18, q19, [%[p], #288]\n\t"
        "ldp q20, q21, [%[p], #320]\n\t"
        "ldp q22, q23, [%[p], #352]\n\t"
        "ldp q24, q25, [%[p], #384]\n\t"
        "ldp q26, q27, [%[p], #416]\n\t"
        "ldp q28, q29, [%[p], #448]\n\t"
        "ldp q30, q31, [%[p], #480]\n\t"
        :
        : [p] "r"(p)
        : "memory");
  }

  // Restore FPCR/FPSR
  uint64_t fpcr = t->fpcr;
  uint64_t fpsr = t->fpsr;
  asm volatile("msr fpcr, %0" :: "r"(fpcr));
  asm volatile("msr fpsr, %0" :: "r"(fpsr));
  asm volatile("isb");
}
