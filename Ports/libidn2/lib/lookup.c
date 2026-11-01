/* lookup.c - implementation of IDNA2008 lookup functions
   Copyright (C) 2011-2025 Simon Josefsson
   Copyright (C) 2017-2025 Tim Ruehsen

   Libidn2 is free software: you can redistribute it and/or modify it
   under the terms of either:

     * the GNU Lesser General Public License as published by the Free
       Software Foundation; either version 3 of the License, or (at
       your option) any later version.

   or

     * the GNU General Public License as published by the Free
       Software Foundation; either version 2 of the License, or (at
       your option) any later version.

   or both in parallel, as here.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received copies of the GNU General Public License and
   the GNU Lesser General Public License along with this program.  If
   not, see <http://www.gnu.org/licenses/>.
*/

#include <config.h>

#include "idn2.h"

#include <errno.h>		/* errno */
#include <stdlib.h>		/* malloc, free */

#include <unitypes.h>
#include <uniconv.h>		/* u8_strconv_from_locale */
#include <uninorm.h>		/* u32_normalize */
#include <unistr.h>		/* u8_to_u32 */

#include "idna.h"		/* _idn2_label_test */
#include "tr46map.h"		/* definition for tr46map.c */

#ifdef HAVE_LIBUNISTRING
/* copied from gnulib */
# include <limits.h>
# define _C_CTYPE_LOWER_N(N) \
   case 'a' + (N): case 'b' + (N): case 'c' + (N): case 'd' + (N): \
   case 'e' + (N): case 'f' + (N): \
   case 'g' + (N): case 'h' + (N): case 'i' + (N): case 'j' + (N): \
   case 'k' + (N): case 'l' + (N): case 'm' + (N): case 'n' + (N): \
   case 'o' + (N): case 'p' + (N): case 'q' + (N): case 'r' + (N): \
   case 's' + (N): case 't' + (N): case 'u' + (N): case 'v' + (N): \
   case 'w' + (N): case 'x' + (N): case 'y' + (N): case 'z' + (N)
# define _C_CTYPE_UPPER _C_CTYPE_LOWER_N ('A' - 'a')
static inline int
c_tolower (int c)
{
  switch (c)
    {
    _C_CTYPE_UPPER:
      return c - 'A' + 'a';
    default:
      return c;
    }
}

static int
c_strncasecmp (const char *s1, const char *s2, size_t n)
{
  register const unsigned char *p1 = (const unsigned char *) s1;
  register const unsigned char *p2 = (const unsigned char *) s2;
  unsigned char c1, c2;

  if (p1 == p2 || n == 0)
    return 0;

  do
    {
      c1 = c_tolower (*p1);
      c2 = c_tolower (*p2);

      if (--n == 0 || c1 == '\0')
	break;

      ++p1;
      ++p2;
    }
  while (c1 == c2);

  if (UCHAR_MAX <= INT_MAX)
    return c1 - c2;
  else
    /* On machines where 'char' and 'int' are types of the same size, the
       difference of two 'unsigned char' values - including the sign bit -
       doesn't fit in an 'int'.  */
    return (c1 > c2 ? 1 : c1 < c2 ? -1 : 0);
}
#else
# include <c-strcase.h>
#endif

static int
set_default_flags (int *flags)
{
  if (((*flags) & IDN2_TRANSITIONAL) && ((*flags) & IDN2_NONTRANSITIONAL))
    return IDN2_INVALID_FLAGS;

  if (((*flags) & (IDN2_TRANSITIONAL | IDN2_NONTRANSITIONAL))
      && ((*flags) & IDN2_NO_TR46))
    return IDN2_INVALID_FLAGS;

  if (((*flags) & IDN2_ALABEL_ROUNDTRIP)
      && ((*flags) & IDN2_NO_ALABEL_ROUNDTRIP))
    return IDN2_INVALID_FLAGS;

  if (!((*flags) & (IDN2_NO_TR46 | IDN2_TRANSITIONAL)))
    *flags |= IDN2_NONTRANSITIONAL;

  return IDN2_OK;
}

static int
label (const uint8_t *src, size_t srclen, uint8_t *dst, size_t *dstlen,
       int flags)
{
  size_t plen;
  uint32_t *p = NULL;
  const uint8_t *src_org = NULL;
  uint8_t *src_allocated = NULL;
  int rc, check_roundtrip = 0;
  size_t tmpl, srclen_org = 0;
  uint32_t label_u32[IDN2_LABEL_MAX_LENGTH];
  size_t label32_len = IDN2_LABEL_MAX_LENGTH;

  if (_idn2_ascii_p (src, srclen))
    {
      if (!(flags & IDN2_NO_ALABEL_ROUNDTRIP) && srclen >= 4
	  && memcmp (src, "xn--", 4) == 0)
	{
	  /*
	     If the input to this procedure appears to be an A-label
	     (i.e., it starts in "xn--", interpreted
	     case-insensitively), the lookup application MAY attempt to
	     convert it to a U-label, first ensuring that the A-label is
	     entirely in lowercase (converting it to lowercase if
	     necessary), and apply the tests of Section 5.4 and the
	     conversion of Section 5.5 to that form. */
	  rc = idn2_punycode_decode ((char *) src + 4, srclen - 4,
				     label_u32, &label32_len);
	  if (rc)
	    return rc;

	  check_roundtrip = 1;
	  src_org = src;
	  srclen_org = srclen;

	  srclen = IDN2_LABEL_MAX_LENGTH;
	  src = src_allocated =
	    u32_to_u8 (label_u32, label32_len, NULL, &srclen);
	  if (!src)
	    {
	      if (errno == ENOMEM)
		return IDN2_MALLOC;
	      return IDN2_ENCODING_ERROR;
	    }
	}
      else
	{
	  if (srclen > IDN2_LABEL_MAX_LENGTH)
	    return IDN2_TOO_BIG_LABEL;
	  if (srclen > *dstlen)
	    return IDN2_TOO_BIG_DOMAIN;

	  memcpy (dst, src, srclen);
	  *dstlen = srclen;
	  return IDN2_OK;
	}
    }

  rc = _idn2_u8_to_u32_nfc (src, srclen, &p, &plen, flags & IDN2_NFC_INPUT);
  if (rc != IDN2_OK)
    goto out;

  if (!(flags & IDN2_TRANSITIONAL))
    {
      rc = _idn2_label_test (TEST_NFC |
			     TEST_2HYPHEN |
			     TEST_LEADING_COMBINING |
			     TEST_DISALLOWED |
			     TEST_CONTEXTJ_RULE |
			     TEST_CONTEXTO_WITH_RULE |
			     TEST_UNASSIGNED | TEST_BIDI |
			     ((flags & IDN2_NONTRANSITIONAL) ?
			      TEST_NONTRANSITIONAL : 0) | ((flags &
							    IDN2_USE_STD3_ASCII_RULES)
							   ? 0 :
							   TEST_ALLOW_STD3_DISALLOWED),
			     p, plen);

      if (rc != IDN2_OK)
	goto out;
    }

  dst[0] = 'x';
  dst[1] = 'n';
  dst[2] = '-';
  dst[3] = '-';

  tmpl = *dstlen - 4;
  rc = idn2_punycode_encode (p, plen, (char *) dst + 4, &tmpl);
  if (rc != IDN2_OK)
    goto out;


  *dstlen = 4 + tmpl;

  if (check_roundtrip)
    {
      if (srclen_org != *dstlen
	  || c_strncasecmp ((char *) src_org, (char *) dst, srclen_org))
	{
	  rc = IDN2_ALABEL_ROUNDTRIP_FAILED;
	  goto out;
	}
    }
  else if (!(flags & IDN2_NO_ALABEL_ROUNDTRIP))
    {
      rc = idn2_punycode_decode ((char *) dst + 4, *dstlen - 4,
				 label_u32, &label32_len);
      if (rc)
	{
	  rc = IDN2_ALABEL_ROUNDTRIP_FAILED;
	  goto out;
	}

      if (plen != label32_len || u32_cmp (p, label_u32, label32_len))
	{
	  rc = IDN2_ALABEL_ROUNDTRIP_FAILED;
	  goto out;
	}
    }

  rc = IDN2_OK;

out:
  free (p);
  free (src_allocated);
  return rc;
}

#define TR46_TRANSITIONAL_CHECK \
  (TEST_NFC | TEST_2HYPHEN | TEST_HYPHEN_STARTEND | TEST_LEADING_COMBINING | TEST_TRANSITIONAL)
#define TR46_NONTRANSITIONAL_CHECK \
  (TEST_NFC | TEST_2HYPHEN | TEST_HYPHEN_STARTEND | TEST_LEADING_COMBINING | TEST_NONTRANSITIONAL)

static int
_tr46 (const uint8_t *domain_u8, uint8_t **out, int flags)
{
  size_t len, it;
  uint32_t *domain_u32;
  int err = IDN2_OK, rc;
  int transitional = 0;
  int test_flags;

  if (flags & IDN2_TRANSITIONAL)
    transitional = 1;

  /* convert UTF-8 to UTF-32 */
  if (!(domain_u32 =
	u8_to_u32 (domain_u8, u8_strlen (domain_u8) + 1, NULL, &len)))
    {
      if (errno == ENOMEM)
	return IDN2_MALLOC;
      return IDN2_ENCODING_ERROR;
    }

  size_t len2 = 0;
  for (it = 0; it < len - 1; it++)
    {
      IDNAMap map;

      get_idna_map (domain_u32[it], &map);

      if (map_is (&map, TR46_FLG_DISALLOWED))
	{
	  if (domain_u32[it])
	    {
	      free (domain_u32);
	      return IDN2_DISALLOWED;
	    }
	  len2++;
	}
      else if (map_is (&map, TR46_FLG_MAPPED))
	{
	  len2 += map.nmappings;
	}
      else if (map_is (&map, TR46_FLG_VALID))
	{
	  len2++;
	}
      else if (map_is (&map, TR46_FLG_IGNORED))
	{
	  continue;
	}
      else if (map_is (&map, TR46_FLG_DEVIATION))
	{
	  if (transitional)
	    {
	      len2 += map.nmappings;
	    }
	  else
	    len2++;
	}
      else if (!(flags & IDN2_USE_STD3_ASCII_RULES))
	{
	  if (map_is (&map, TR46_FLG_DISALLOWED_STD3_VALID))
	    {
	      /* valid because UseSTD3ASCIIRules=false, see #TR46 5 */
	      len2++;
	    }
	  else if (map_is (&map, TR46_FLG_DISALLOWED_STD3_MAPPED))
	    {
	      /* mapped because UseSTD3ASCIIRules=false, see #TR46 5 */
	      len2 += map.nmappings;
	    }
	}
    }

  /* Exit early if result is too long.
   * This avoids excessive CPU usage in punycode encoding, which is O(N^2). */
  if (len2 >= IDN2_DOMAIN_MAX_LENGTH)
    {
      free (domain_u32);
      return IDN2_TOO_BIG_DOMAIN;
    }

  uint32_t *tmp = (uint32_t *) malloc ((len2 + 1) * sizeof (uint32_t));
  if (!tmp)
    {
      free (domain_u32);
      return IDN2_MALLOC;
    }

  len2 = 0;
  for (it = 0; it < len - 1; it++)
    {
      uint32_t c = domain_u32[it];
      IDNAMap map;

      get_idna_map (c, &map);

      if (map_is (&map, TR46_FLG_DISALLOWED))
	{
	  tmp[len2++] = c;
	}
      else if (map_is (&map, TR46_FLG_MAPPED))
	{
	  len2 += get_map_data (tmp + len2, &map);
	}
      else if (map_is (&map, TR46_FLG_VALID))
	{
	  tmp[len2++] = c;
	}
      else if (map_is (&map, TR46_FLG_IGNORED))
	{
	  continue;
	}
      else if (map_is (&map, TR46_FLG_DEVIATION))
	{
	  if (transitional)
	    {
	      len2 += get_map_data (tmp + len2, &map);
	    }
	  else
	    tmp[len2++] = c;
	}
      else if (!(flags & IDN2_USE_STD3_ASCII_RULES))
	{
	  if (map_is (&map, TR46_FLG_DISALLOWED_STD3_VALID))
	    {
	      tmp[len2++] = c;
	    }
	  else if (map_is (&map, TR46_FLG_DISALLOWED_STD3_MAPPED))
	    {
	      len2 += get_map_data (tmp + len2, &map);
	    }
	}
    }
  free (domain_u32);

  /* Normalize to NFC */
  tmp[len2] = 0;
  domain_u32 = u32_normalize (UNINORM_NFC, tmp, len2 + 1, NULL, &len);
  free (tmp);
  tmp = NULL;

  if (!domain_u32)
    {
      if (errno == ENOMEM)
	return IDN2_MALLOC;
      return IDN2_ENCODING_ERROR;
    }

  /* split into labels and check */
  uint32_t *e, *s;
  for (e = s = domain_u32; *e; s = e)
    {
      while (*e && *e != '.')
	e++;

      if (e - s >= 4 && s[0] == 'x' && s[1] == 'n' && s[2] == '-'
	  && s[3] == '-')
	{
	  /* decode punycode and check result non-transitional */
	  size_t ace_len;
	  uint32_t name_u32[IDN2_LABEL_MAX_LENGTH];
	  size_t name_len = IDN2_LABEL_MAX_LENGTH;
	  uint8_t *ace;

	  ace = u32_to_u8 (s + 4, e - s - 4, NULL, &ace_len);
	  if (!ace)
	    {
	      free (domain_u32);
	      if (errno == ENOMEM)
		return IDN2_MALLOC;
	      return IDN2_ENCODING_ERROR;
	    }

	  rc = idn2_punycode_decode ((char *) ace, ace_len,
				     name_u32, &name_len);

	  free (ace);

	  if (rc)
	    {
	      free (domain_u32);
	      return rc;
	    }

	  test_flags = TR46_NONTRANSITIONAL_CHECK;

	  if (!(flags & IDN2_USE_STD3_ASCII_RULES))
	    test_flags |= TEST_ALLOW_STD3_DISALLOWED;

	  if ((rc = _idn2_label_test (test_flags, name_u32, name_len)))
	    err = rc;
	}
      else
	{
	  test_flags =
	    transitional ? TR46_TRANSITIONAL_CHECK :
	    TR46_NONTRANSITIONAL_CHECK;

	  if (!(flags & IDN2_USE_STD3_ASCII_RULES))
	    test_flags |= TEST_ALLOW_STD3_DISALLOWED;

	  if ((rc = _idn2_label_test (test_flags, s, e - s)))
	    err = rc;
	}

      if (*e)
	e++;
    }

  if (err == IDN2_OK && out)
    {
      uint8_t *_out = u32_to_u8 (domain_u32, len, NULL, &len);
      free (domain_u32);

      if (!_out)
	{
	  if (errno == ENOMEM)
	    return IDN2_MALLOC;
	  return IDN2_ENCODING_ERROR;
	}

      *out = _out;
    }
  else
    free (domain_u32);

  return err;
}

/**
 * idn2_lookup_u8:
 * @src: input zero-terminated UTF-8 string in Unicode NFC normalized form.
 * @lookupname: newly allocated output variable with name to lookup in DNS.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Perform IDNA2008 lookup string conversion on domain name @src, as
 * described in section 5 of RFC 5891.  Note that the input string
 * must be encoded in UTF-8 and be in Unicode NFC form.
 *
 * Pass %IDN2_NFC_INPUT in @flags to convert input to NFC form before
 * further processing.  %IDN2_TRANSITIONAL and %IDN2_NONTRANSITIONAL
 * do already imply %IDN2_NFC_INPUT.
 *
 * Pass %IDN2_ALABEL_ROUNDTRIP in @flags to
 * convert any input A-labels to U-labels and perform additional
 * testing. This is default since version 2.2.
 * To switch this behavior off, pass IDN2_NO_ALABEL_ROUNDTRIP
 *
 * Pass %IDN2_TRANSITIONAL to enable Unicode TR46
 * transitional processing, and %IDN2_NONTRANSITIONAL to enable
 * Unicode TR46 non-transitional processing.
 *
 * Multiple flags may be specified by binary or:ing them together.
 *
 * After version 2.0.3: %IDN2_USE_STD3_ASCII_RULES disabled by default.
 * Previously we were eliminating non-STD3 characters from domain strings
 * such as _443._tcp.example.com, or IPs 1.2.3.4/24 provided to libidn2
 * functions. That was an unexpected regression for applications switching
 * from libidn and thus it is no longer applied by default.
 * Use %IDN2_USE_STD3_ASCII_RULES to enable that behavior again.
 *
 * After version 0.11: @lookupname may be NULL to test lookup of @src
 * without allocating memory.
 *
 * Returns: On successful conversion %IDN2_OK is returned, if the
 *   output domain or any label would have been too long
 *   %IDN2_TOO_BIG_DOMAIN or %IDN2_TOO_BIG_LABEL is returned, or
 *   another error code is returned.
 *
 * Since: 0.1
 **/
int
idn2_lookup_u8 (const uint8_t *src, uint8_t **lookupname, int flags)
{
  size_t lookupnamelen = 0;
  uint8_t _lookupname[IDN2_DOMAIN_MAX_LENGTH + 1];
  uint8_t *src_allocated = NULL;
  int rc;

  if (src == NULL)
    {
      if (lookupname)
	*lookupname = NULL;
      return IDN2_OK;
    }

  rc = set_default_flags (&flags);
  if (rc != IDN2_OK)
    return rc;

  if (!(flags & IDN2_NO_TR46))
    {
      uint8_t *out = NULL;

      rc = _tr46 (src, &out, flags);
      if (rc != IDN2_OK)
	return rc;

      src = src_allocated = out;
    }

  do
    {
      const uint8_t *end = (uint8_t *) strchrnul ((const char *) src, '.');
      /* XXX Do we care about non-U+002E dots such as U+3002, U+FF0E
         and U+FF61 here?  Perhaps when IDN2_NFC_INPUT? */
      size_t labellen = end - src;
      uint8_t tmp[IDN2_LABEL_MAX_LENGTH];
      size_t tmplen = IDN2_LABEL_MAX_LENGTH;

      rc = label (src, labellen, tmp, &tmplen, flags);
      if (rc != IDN2_OK)
	{
	  free (src_allocated);
	  return rc;
	}

      if (lookupnamelen + tmplen
	  > IDN2_DOMAIN_MAX_LENGTH - (tmplen == 0 && *end == '\0' ? 1 : 2))
	{
	  free (src_allocated);
	  return IDN2_TOO_BIG_DOMAIN;
	}

      memcpy (_lookupname + lookupnamelen, tmp, tmplen);
      lookupnamelen += tmplen;

      if (*end == '.')
	{
	  if (lookupnamelen + 1 > IDN2_DOMAIN_MAX_LENGTH)
	    {
	      free (src_allocated);
	      return IDN2_TOO_BIG_DOMAIN;
	    }

	  _lookupname[lookupnamelen] = '.';
	  lookupnamelen++;
	}
      _lookupname[lookupnamelen] = '\0';

      src = end;
    }
  while (*src++);

  free (src_allocated);

  if (lookupname)
    {
      uint8_t *tmp = (uint8_t *) malloc (lookupnamelen + 1);

      if (tmp == NULL)
	return IDN2_MALLOC;

      memcpy (tmp, _lookupname, lookupnamelen + 1);
      *lookupname = tmp;
    }

  return IDN2_OK;
}

/**
 * idn2_lookup_ul:
 * @src: input zero-terminated locale encoded string.
 * @lookupname: newly allocated output variable with name to lookup in DNS.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Perform IDNA2008 lookup string conversion on domain name @src, as
 * described in section 5 of RFC 5891.  Note that the input is assumed
 * to be encoded in the locale's default coding system, and will be
 * transcoded to UTF-8 and NFC normalized by this function.
 *
 * Pass %IDN2_ALABEL_ROUNDTRIP in @flags to
 * convert any input A-labels to U-labels and perform additional
 * testing. This is default since version 2.2.
 * To switch this behavior off, pass IDN2_NO_ALABEL_ROUNDTRIP
 *
 * Pass %IDN2_TRANSITIONAL to enable Unicode TR46 transitional processing,
 * and %IDN2_NONTRANSITIONAL to enable Unicode TR46 non-transitional
 * processing.
 *
 * Multiple flags may be specified by binary or:ing them together, for
 * example %IDN2_ALABEL_ROUNDTRIP | %IDN2_NONTRANSITIONAL.
 *
 * The %IDN2_NFC_INPUT in @flags is always enabled in this function.
 *
 * After version 0.11: @lookupname may be NULL to test lookup of @src
 * without allocating memory.
 *
 * Returns: On successful conversion %IDN2_OK is returned, if
 *   conversion from locale to UTF-8 fails then %IDN2_ICONV_FAIL is
 *   returned, if the output domain or any label would have been too
 *   long %IDN2_TOO_BIG_DOMAIN or %IDN2_TOO_BIG_LABEL is returned, or
 *   another error code is returned.
 *
 * Since: 0.1
 **/
int
idn2_lookup_ul (const char *src, char **lookupname, int flags)
{
  uint8_t *utf8src = NULL;
  int rc;

  if (src)
    {
      const char *encoding = locale_charset ();

      utf8src = u8_strconv_from_encoding (src, encoding, iconveh_error);

      if (!utf8src)
	{
	  if (errno == ENOMEM)
	    return IDN2_MALLOC;
	  return IDN2_ICONV_FAIL;
	}
    }

  rc = idn2_lookup_u8 (utf8src, (uint8_t **) lookupname,
		       flags | IDN2_NFC_INPUT);

  free (utf8src);

  return rc;
}

/**
 * idn2_to_ascii_4i:
 * @input: zero terminated input Unicode (UCS-4) string.
 * @inlen: number of elements in @input.
 * @output: output zero terminated string that must have room for at
 *       least 63 characters plus the terminating zero.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * The ToASCII operation takes a sequence of Unicode code points that make
 * up one domain label and transforms it into a sequence of code points in
 * the ASCII range (0..7F). If ToASCII succeeds, the original sequence and
 * the resulting sequence are equivalent labels.
 *
 * It is important to note that the ToASCII operation can fail.
 * ToASCII fails if any step of it fails. If any step of the
 * ToASCII operation fails on any label in a domain name, that domain
 * name MUST NOT be used as an internationalized domain name.
 * The method for dealing with this failure is application-specific.
 *
 * The inputs to ToASCII are a sequence of code points.
 *
 * ToASCII never alters a sequence of code points that are all in the ASCII
 * range to begin with (although it could fail). Applying the ToASCII operation multiple
 * effect as applying it just once.
 *
 * The default behavior of this function (when flags are zero) is to apply
 * the IDNA2008 rules without the TR46 amendments. As the TR46
 * non-transitional processing is nowadays ubiquitous, when unsure, it is
 * recommended to call this function with the %IDN2_NONTRANSITIONAL
 * and the %IDN2_NFC_INPUT flags for compatibility with other software.
 *
 * Warning: With version 2.1.1 until before version 2.3.5 this
 * function was deprecated in favor idn2_to_ascii_4i2().  We still
 * encourage you to use idn2_to_ascii_4i2() when appropriate.
 *
 * Returns: On successful conversion %IDN2_OK is returned; if the
 *   output label would have been too long %IDN2_TOO_BIG_LABEL is
 *   returned, or another error code is returned.
 *
 * Since: 2.0.0
 **/
int
idn2_to_ascii_4i (const uint32_t *input, size_t inlen, char *output,
		  int flags)
{
  char *out;
  int rc;

  if (!input)
    {
      if (output)
	*output = 0;
      return IDN2_OK;
    }

  rc = idn2_to_ascii_4i2 (input, inlen, &out, flags);
  if (rc == IDN2_OK)
    {
      size_t len = strlen (out);

      if (len > IDN2_LABEL_MAX_LENGTH)
	rc = IDN2_TOO_BIG_LABEL;
      else if (output)
	strcpy (output, out);

      free (out);
    }

  return rc;
}

/**
 * idn2_to_ascii_4i2:
 * @input: zero terminated input Unicode (UCS-4) string.
 * @inlen: number of elements in @input.
 * @output: pointer to newly allocated zero-terminated output string.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * The ToASCII operation takes a sequence of Unicode code points that make
 * up one domain label and transforms it into a sequence of code points in
 * the ASCII range (0..7F). If ToASCII succeeds, the original sequence and
 * the resulting sequence are equivalent labels.
 *
 * It is important to note that the ToASCII operation can fail.
 * ToASCII fails if any step of it fails. If any step of the
 * ToASCII operation fails on any label in a domain name, that domain
 * name MUST NOT be used as an internationalized domain name.
 * The method for dealing with this failure is application-specific.
 *
 * The inputs to ToASCII are a sequence of code points.
 *
 * ToASCII never alters a sequence of code points that are all in the ASCII
 * range to begin with (although it could fail). Applying the ToASCII operation multiple
 * effect as applying it just once.
 *
 * The default behavior of this function (when flags are zero) is to apply
 * the IDNA2008 rules without the TR46 amendments. As the TR46
 * non-transitional processing is nowadays ubiquitous, when unsure, it is
 * recommended to call this function with the %IDN2_NONTRANSITIONAL
 * and the %IDN2_NFC_INPUT flags for compatibility with other software.
 *
 * Returns: On successful conversion %IDN2_OK is returned; if the
 *   output label would have been too long %IDN2_TOO_BIG_LABEL is
 *   returned, or another error code is returned.
 *
 * Since: 2.1.1
 **/
int
idn2_to_ascii_4i2 (const uint32_t *input, size_t inlen, char **output,
		   int flags)
{
  uint32_t *input_u32;
  uint8_t *input_u8, *output_u8;
  size_t length;
  int rc;

  if (!input)
    {
      if (output)
	*output = NULL;
      return IDN2_OK;
    }

  input_u32 = (uint32_t *) malloc ((inlen + 1) * sizeof (uint32_t));
  if (!input_u32)
    return IDN2_MALLOC;

  u32_cpy (input_u32, input, inlen);
  input_u32[inlen] = 0;

  input_u8 = u32_to_u8 (input_u32, inlen + 1, NULL, &length);
  free (input_u32);
  if (!input_u8)
    {
      if (errno == ENOMEM)
	return IDN2_MALLOC;
      return IDN2_ENCODING_ERROR;
    }

  rc = idn2_lookup_u8 (input_u8, &output_u8, flags);
  free (input_u8);

  if (rc == IDN2_OK)
    {
      if (output)
	*output = (char *) output_u8;
      else
	free (output_u8);
    }

  return rc;
}

/**
 * idn2_to_ascii_4z:
 * @input: zero terminated input Unicode (UCS-4) string.
 * @output: pointer to newly allocated zero-terminated output string.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Convert UCS-4 domain name to ASCII string using the IDNA2008
 * rules.  The domain name may contain several labels, separated by dots.
 * The output buffer must be deallocated by the caller.
 *
 * The default behavior of this function (when flags are zero) is to apply
 * the IDNA2008 rules without the TR46 amendments. As the TR46
 * non-transitional processing is nowadays ubiquitous, when unsure, it is
 * recommended to call this function with the %IDN2_NONTRANSITIONAL
 * and the %IDN2_NFC_INPUT flags for compatibility with other software.
 *
 * Return value: Returns %IDN2_OK on success, or error code.
 *
 * Since: 2.0.0
 **/
int
idn2_to_ascii_4z (const uint32_t *input, char **output, int flags)
{
  uint8_t *input_u8;
  size_t length;
  int rc;

  if (!input)
    {
      if (output)
	*output = NULL;
      return IDN2_OK;
    }

  input_u8 = u32_to_u8 (input, u32_strlen (input) + 1, NULL, &length);
  if (!input_u8)
    {
      if (errno == ENOMEM)
	return IDN2_MALLOC;
      return IDN2_ENCODING_ERROR;
    }

  rc = idn2_lookup_u8 (input_u8, (uint8_t **) output, flags);
  free (input_u8);

  return rc;
}

/**
 * idn2_to_ascii_8z:
 * @input: zero terminated input UTF-8 string.
 * @output: pointer to newly allocated output string.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Convert UTF-8 domain name to ASCII string using the IDNA2008
 * rules.  The domain name may contain several labels, separated by dots.
 * The output buffer must be deallocated by the caller.
 *
 * The default behavior of this function (when flags are zero) is to apply
 * the IDNA2008 rules without the TR46 amendments. As the TR46
 * non-transitional processing is nowadays ubiquitous, when unsure, it is
 * recommended to call this function with the %IDN2_NONTRANSITIONAL
 * and the %IDN2_NFC_INPUT flags for compatibility with other software.
 *
 * Return value: Returns %IDN2_OK on success, or error code.
 *
 * Since: 2.0.0
 **/
int
idn2_to_ascii_8z (const char *input, char **output, int flags)
{
  return idn2_lookup_u8 ((const uint8_t *) input, (uint8_t **) output, flags);
}

/**
 * idn2_to_ascii_lz:
 * @input: zero terminated input UTF-8 string.
 * @output: pointer to newly allocated output string.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Convert a domain name in locale's encoding to ASCII string using the IDNA2008
 * rules.  The domain name may contain several labels, separated by dots.
 * The output buffer must be deallocated by the caller.
 *
 * The default behavior of this function (when flags are zero) is to apply
 * the IDNA2008 rules without the TR46 amendments. As the TR46
 * non-transitional processing is nowadays ubiquitous, when unsure, it is
 * recommended to call this function with the %IDN2_NONTRANSITIONAL
 * and the %IDN2_NFC_INPUT flags for compatibility with other software.
 *
 * Returns: %IDN2_OK on success, or error code.
 * Same as described in idn2_lookup_ul() documentation.
 *
 * Since: 2.0.0
 **/
int
idn2_to_ascii_lz (const char *input, char **output, int flags)
{
  return idn2_lookup_ul (input, output, flags);
}
