#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Priority inversion lab:
// - mode=1: demonstrate inversion, then enable PI and recover (expected PASS)
void sync_lab_setup(unsigned mode);

#ifdef __cplusplus
}
#endif

