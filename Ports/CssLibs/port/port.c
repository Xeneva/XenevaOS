/**
 * CssLibs port shims for XenevaOS.
 *
 * bsearch: used once by libparserutils (charset/aliases.c). XEClib does
 * not provide it, so a small self-contained binary search lives here.
 *
 * abort trap: XEClib's abort() exits silently, which makes libcss asserts
 * undebuggable (a dead window looks "stuck"). Route port aborts through
 * here so the serial log names the call site; resolve with
 * llvm-objdump on the linked binary.
 */

#include <stddef.h>
#include <_xeneva.h>
#include <sys/_keproc.h>

void ns_abort_trap(void) {
    void* ra = __builtin_return_address(0);
    /* NOTE: the _KePrint backend (UARTDebugOut) only formats %d %c %x
     * %s %f %% -- no %u/%p. size_t is 64-bit, so %x prints the address. */
    _KePrint("[cssabort] libcss assert, return address 0x%x\n", (size_t)ra);
    _KeProcessExit();
    for (;;) {
    }
}

void* bsearch(const void* key, const void* base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*)) {
    size_t lo = 0;
    size_t hi = nmemb;
    const unsigned char* b = (const unsigned char*)base;

    if (!key || !base || !compar || size == 0)
        return NULL;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        const void* elem = (const void*)(b + mid * size);
        int c = compar(key, elem);
        if (c == 0)
            return (void*)elem;
        if (c < 0)
            hi = mid;
        else
            lo = mid + 1;
    }
    return NULL;
}
