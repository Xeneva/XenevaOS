/* Locale and iconv shims for the XenevaOS libidn2 port.
 *
 * XenevaOS uses UTF-8 everywhere and XEClib does not provide iconv or
 * nl_langinfo.  libidn2 only calls these helpers from its locale-based
 * wrappers (idn2_lookup_ul, idn2_to_unicode_lzlz, ...).  Treat the
 * locale as UTF-8 and make UTF-8/ASCII conversions an identity copy.
 */
#include <config.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <uniconv.h>
#include <localcharset.h>

const char *
locale_charset (void)
{
  return "UTF-8";
}

/* XEClib does not implement strchrnul(). */
char *
strchrnul (const char *s, int c)
{
  const char *p = s;

  while (*p && *p != (char) c)
    p++;

  return (char *) p;
}

/* XEClib declares bsearch() but does not provide it. */
#ifdef bsearch
# undef bsearch
#endif
void *
bsearch (const void *key, const void *base, size_t nmemb, size_t size,
         int (*compar) (const void *, const void *))
{
  size_t lo = 0;
  size_t hi = nmemb;
  const unsigned char *bytes = (const unsigned char *) base;

  if (!key || !base || !compar || size == 0)
    return NULL;

  while (lo < hi)
    {
      size_t mid = lo + (hi - lo) / 2;
      const void *elem = bytes + mid * size;
      int cmp = compar (key, elem);

      if (cmp == 0)
        return (void *) elem;
      if (cmp < 0)
        hi = mid;
      else
        lo = mid + 1;
    }

  return NULL;
}

/* XEClib does not implement strverscmp().  This is the common
 * digit-run-aware comparison, sufficient for libidn2 version checks. */
int
strverscmp (const char *s1, const char *s2)
{
  const unsigned char *p1 = (const unsigned char *) s1;
  const unsigned char *p2 = (const unsigned char *) s2;

  while (*p1 && *p2)
    {
      if (*p1 >= '0' && *p1 <= '9' && *p2 >= '0' && *p2 <= '9')
        {
          unsigned long n1 = 0, n2 = 0;

          while (*p1 >= '0' && *p1 <= '9')
            n1 = n1 * 10 + (*p1++ - '0');
          while (*p2 >= '0' && *p2 <= '9')
            n2 = n2 * 10 + (*p2++ - '0');

          if (n1 != n2)
            return n1 < n2 ? -1 : 1;
        }
      else
        {
          if (*p1 != *p2)
            return *p1 < *p2 ? -1 : 1;
          p1++;
          p2++;
        }
    }

  return *p1 - *p2;
}

static int
is_utf8_name (const char *name)
{
  return name
    && (strcasecmp (name, "UTF-8") == 0
        || strcasecmp (name, "UTF8") == 0
        || strcasecmp (name, "ASCII") == 0
        || strcasecmp (name, "US-ASCII") == 0
        || strcasecmp (name, "ANSI_X3.4-1968") == 0);
}

uint8_t *
u8_strconv_from_encoding (const char *string, const char *fromcode,
                          enum iconv_ilseq_handler handler)
{
  size_t len;
  uint8_t *result;

  (void) handler;

  if (!string)
    return NULL;

  if (!is_utf8_name (fromcode))
    {
      errno = EINVAL;
      return NULL;
    }

  len = strlen (string) + 1;
  result = malloc (len);
  if (!result)
    {
      errno = ENOMEM;
      return NULL;
    }

  memcpy (result, string, len);
  return result;
}

char *
u8_strconv_to_encoding (const uint8_t *string, const char *tocode,
                        enum iconv_ilseq_handler handler)
{
  size_t len;
  char *result;

  (void) handler;

  if (!string)
    return NULL;

  if (!is_utf8_name (tocode))
    {
      errno = EINVAL;
      return NULL;
    }

  len = strlen ((const char *) string) + 1;
  result = malloc (len);
  if (!result)
    {
      errno = ENOMEM;
      return NULL;
    }

  memcpy (result, string, len);
  return result;
}
