/* XEClib shims for the XenevaOS libpsl port. */
#include <config.h>

#include <stdio.h>

/* XEClib provides fgetc() but not fgets().  psl_load_fp() uses fgets()
 * to parse the textual public-suffix list. */
char *
fgets (char *s, int size, struct _IO_FILE_ *stream)
{
  int i = 0;
  int c;

  if (!s || size <= 0 || !stream)
    return NULL;

  while (i < size - 1)
    {
      c = fgetc (stream);
      if (c == -1)
        break;
      s[i++] = (char) c;
      if (c == '\n')
        break;
    }

  if (i == 0)
    return NULL;

  s[i] = '\0';
  return s;
}
