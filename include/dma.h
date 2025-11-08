#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dma_cb_t)(void* user, int status);

int dma_submit_memcpy(void* dst, const void* src, size_t len,
                      dma_cb_t cb, void* user);
void dma_poll_complete(void);

#ifdef __cplusplus
}
#endif
