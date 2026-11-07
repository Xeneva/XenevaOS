/* Runtime switch consulted by the LSE outline-atomic helpers.
 *
 * compiler-rt normally defines __aarch64_have_lse_atomics in
 * cpu_model/aarch64.c, but that file needs the MSVC SDK (windows.h and
 * IsProcessorFeaturePresent) plus a constructor.  XenevaOS has neither, so
 * the flag stays zero and every outline atomic uses the LDXR/STXR fallback,
 * which every ARMv8 CPU implements.  A cpu_model port can set it at boot once
 * the target ABI has a way to probe ID_AA64ISAR0_EL1.
 */
#if defined(__aarch64__)

_Bool __aarch64_have_lse_atomics = 0;

#endif
