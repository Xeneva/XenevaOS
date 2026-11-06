/* Minimal config.h for the XenevaOS libpsl port.
 *
 * The library is built with the public-suffix data compiled in
 * (ENABLE_BUILTIN) and without runtime IDNA support.  XenevaOS has no
 * iconv, so the built-in punycode conversion in psl.c is used instead.
 */
#ifndef XE_LIBPSL_CONFIG_H
#define XE_LIBPSL_CONFIG_H

#define PACKAGE "libpsl"
#define PACKAGE_NAME "libpsl"
#define PACKAGE_STRING "libpsl 0.23.3"
#define PACKAGE_VERSION "0.23.3"
#define VERSION "0.23.3"

#define ENABLE_BUILTIN 1
#define HAVE_CONFIG_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_UNISTD_H 1
#define HAVE_VISIBILITY 1

/* XEClib has no fgets(); port/xe_port.c provides it. */
#ifdef __cplusplus
extern "C" {
#endif
struct _IO_FILE_;
extern char *fgets (char *s, int size, struct _IO_FILE_ *stream);
#ifdef __cplusplus
}
#endif

#endif
