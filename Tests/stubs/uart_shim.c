/* Host-side UARTDebugOut shim for harnesses that don't define their own.
 * Forwards to vprintf. See alloc_debug_stress.c for the capturing variant.
 */
#include <stdio.h>
#include <stdarg.h>

void UARTDebugOut(const char *format, ...) {
	va_list ap;
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
}
