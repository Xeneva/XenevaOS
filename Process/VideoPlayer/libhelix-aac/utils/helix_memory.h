#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void* helix_malloc(int size);
void helix_free(void *ptr);

#ifdef __cplusplus
}
#endif
