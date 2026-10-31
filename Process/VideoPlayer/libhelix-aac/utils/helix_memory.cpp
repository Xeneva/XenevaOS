#include "Allocator.h"
#include "helix_log.h"
#include "ConfigHelix.h"

char log_buffer_helix[HELIX_LOG_SIZE];
ALLOCATOR alloc;

#ifdef __cplusplus
extern "C" {
#endif

void* helix_malloc(int size) { return alloc.allocate(size); }

void helix_free(void* ptr) { alloc.free(ptr); }

#ifdef __cplusplus
}
#endif
