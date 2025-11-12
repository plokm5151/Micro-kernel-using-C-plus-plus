#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void except_el1_sync(uint64_t esr, uint64_t elr, uint64_t far, uint64_t spsr, uint64_t vector_tag);
#ifdef __cplusplus
}
#endif
