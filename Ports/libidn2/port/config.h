/* Minimal config.h for the XenevaOS libidn2 port.
 *
 * This is a hand-written replacement for the config.h that libidn2's
 * configure script generates.  The port builds the bundled unistring
 * subset that ships with libidn2, so the external-libunistring and
 * iconv probes are deliberately left disabled.
 */
#ifndef XE_LIBIDN2_CONFIG_H
#define XE_LIBIDN2_CONFIG_H

#define _GL_CONFIG_H_INCLUDED 1

#include <stdbool.h>

#define PACKAGE "libidn2"
#define PACKAGE_NAME "libidn2"
#define PACKAGE_STRING "libidn2 2.3.8"
#define PACKAGE_VERSION "2.3.8"
#define PACKAGE_BUGREPORT "help-libidn@gnu.org"
#define PACKAGE_URL "https://www.gnu.org/software/libidn/#libidn2"
#define VERSION "2.3.8"
#define LOCALEDIR "/usr/share/locale"

/* libidn2's bundled unistring is built here, not linked from the system. */
/* #undef HAVE_LIBUNISTRING */

/* No compiler-inline fast paths: the library must provide every symbol. */
#define HAVE_INLINE 0

/* The generated unistring headers only declare the multibyte helpers when
 * the matching gnulib module macro is set.  The port compiles all of these
 * helpers, so enable their declarations. */
#define GNULIB_UNISTR_U8_MBTOUC 1
#define GNULIB_UNISTR_U8_MBTOUC_UNSAFE 1
#define GNULIB_UNISTR_U8_MBTOUCR 1
#define GNULIB_UNISTR_U8_UCTOMB 1
#define GNULIB_UNISTR_U16_MBTOUC 1
#define GNULIB_UNISTR_U16_MBTOUC_UNSAFE 1
#define GNULIB_UNISTR_U16_MBTOUCR 1
#define GNULIB_UNISTR_U16_UCTOMB 1
#define GNULIB_UNISTR_U32_MBTOUC 1
#define GNULIB_UNISTR_U32_MBTOUC_UNSAFE 1
#define GNULIB_UNISTR_U32_MBTOUCR 1
#define GNULIB_UNISTR_U32_UCTOMB 1

/* free() is C89; the gnulib replacement is unnecessary. */
#define HAVE_FREE_POSIX 1

/* Hide internal symbols in shared-library builds. */
#define HAVE_VISIBILITY 1

/* No wide-character, locale or iconv support is used by the port. */
/* #undef HAVE_LANGINFO_CODESET */
/* #undef HAVE_ICONV */
/* #undef HAVE_WCHAR_H */

#define GNULIB_NO_VLA 1

/* The gnulib-generated unistring headers expect these attribute helpers,
 * normally supplied by gnulib's full config.h.  Keep them conservative:
 * clang supports the common ones, and the deallocation annotations are
 * omitted because they add nothing to a static library build. */
#define _GL_ATTRIBUTE_MALLOC __attribute__ ((__malloc__))
#define _GL_ATTRIBUTE_DEALLOC(f, i)
#define _GL_ATTRIBUTE_DEALLOC_FREE
#define _GL_ATTRIBUTE_MAYBE_UNUSED __attribute__ ((__unused__))
#define _GL_ATTRIBUTE_SENTINEL(pos) __attribute__ ((__sentinel__ pos))
#define _GL_ATTRIBUTE_PURE __attribute__ ((__pure__))
#define _GL_ATTRIBUTE_CONST __attribute__ ((__const__))
#define _GL_ATTRIBUTE_FALLTHROUGH ((void) 0)

/* gnulib inline helpers, normally supplied by its generated config.h. */
#define _GL_INLINE static inline
#define _GL_EXTERN_INLINE extern
#define _GL_INLINE_HEADER_BEGIN
#define _GL_INLINE_HEADER_END
#define _GL_CMP(n1, n2) (((n1) > (n2)) - ((n1) < (n2)))

/* XEClib has no strchrnul() or strverscmp(); port/xe_port.c provides them. */
#ifdef __cplusplus
extern "C" {
#endif
extern char *strchrnul (const char *s, int c);
extern int strverscmp (const char *s1, const char *s2);
#ifdef __cplusplus
}
#endif

#endif
