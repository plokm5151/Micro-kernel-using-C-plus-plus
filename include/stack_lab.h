#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Stack lab:
// - mode=1: probe guard page (expected synchronous exception on access)
void stack_lab_setup(unsigned mode);

#ifdef __cplusplus
}
#endif

