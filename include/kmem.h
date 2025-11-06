#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void   kmem_init(void);
void*  kmem_alloc_aligned(size_t size, size_t align); // align is power of two
#ifdef __cplusplus
}
#endif
