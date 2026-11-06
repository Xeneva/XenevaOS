/* Host libc supplies struct timeval; the freestanding stdint.h supplies
 * the kernel's timeval typedef. Keep the real kernel headers otherwise. */
#include <stddef.h>
#include <stdlib.h>
#include <sys/time.h>
#define ALIGN_UP(x, y) (((x) + (y) - 1) / (y) * (y))
typedef struct timeval timeval;
