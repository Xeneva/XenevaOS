/* register.c - implementation of IDNA2008 register functions
   Copyright (C) 2011-2025 Simon Josefsson

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
#include <stdlib.h>		/* free */

#include <unitypes.h>
#include <uniconv.h>		/* u8_strconv_from_locale */
#include <unistr.h>		/* u32_to_u8 */

#include "idna.h"		/* _idn2_label_test */

/**
 * idn2_register_u8:
 * @ulabel: input zero-terminated UTF-8 and Unicode NFC string, or NULL.
 * @alabel: input zero-terminated ACE encoded string (xn--), or NULL.
 * @insertname: newly allocated output variable with name to register in DNS.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Perform IDNA2008 register string conversion on domain label @ulabel
 * and @alabel, as described in section 4 of RFC 5891.  Note that the
 * input @ulabel must be encoded in UTF-8 and be in Unicode NFC form.
 *
 * Pass %IDN2_NFC_INPUT in @flags to convert input @ulabel to NFC form
 * before further processing.
 *
 * It is recommended to supply both @ulabel and @alabel for better
 * error checking, but supplying just one of them will work.  Passing
 * in only @alabel is better than only @ulabel.  See RFC 5891 section
 * 4 for more information.
 *
 * After version 0.11: @insertname may be NULL to test conversion of @src
 * without allocating memory.
 *
 * Returns: On successful conversion %IDN2_OK is returned, when the
 *   given @ulabel and @alabel does not match each other
 *   %IDN2_UALABEL_MISMATCH is returned, when either of the input
 *   labels are too long %IDN2_TOO_BIG_LABEL is returned, when @alabel
 *   does does not appear to be a proper A-label %IDN2_INVALID_ALABEL
 *   is returned, or another error code is returned.
 **/
int
idn2_register_u8 (const uint8_t *ulabel, const uint8_t *alabel,
		  uint8_t **insertname, int flags)
{
  int rc;

  if (ulabel == NULL && alabel == NULL)
    {
      if (insertname)
	*insertname = NULL;
      return IDN2_OK;
    }

  if (alabel)
    {
      size_t alabellen = strlen ((char *) alabel), u32len =
	IDN2_LABEL_MAX_LENGTH * 4;
      uint32_t u32[IDN2_DOMAIN_MAX_LENGTH * 4];
      uint8_t *tmp;
      uint8_t u8[IDN2_DOMAIN_MAX_LENGTH + 1];
      size_t u8len;

      if (alabellen > IDN2_LABEL_MAX_LENGTH)
	return IDN2_TOO_BIG_LABEL;

      if (alabellen <= 4)
	return IDN2_INVALID_ALABEL;
      if (alabel[0] != 'x'
	  || alabel[1] != 'n' || alabel[2] != '-' || alabel[3] != '-')
	return IDN2_INVALID_ALABEL;

      if (!_idn2_ascii_p (alabel, alabellen))
	return IDN2_INVALID_ALABEL;

      rc = idn2_punycode_decode ((char *) alabel + 4, alabellen - 4,
				 u32, &u32len);
      if (rc != IDN2_OK)
	return rc;

      u8len = sizeof (u8);
      if (u32_to_u8 (u32, u32len, u8, &u8len) == NULL)
	return IDN2_ENCODING_ERROR;
      u8[u8len] = '\0';

      if (ulabel)
	{
	  if (strcmp ((char *) ulabel, (char *) u8) != 0)
	    return IDN2_UALABEL_MISMATCH;
	}

      rc = idn2_register_u8 (u8, NULL, &tmp, 0);
      if (rc != IDN2_OK)
	return rc;

      rc = strcmp ((char *) alabel, (char *) tmp);
      free (tmp);
      if (rc != 0)
	return IDN2_UALABEL_MISMATCH;

      if (insertname)
	{
	  uint8_t *m = (uint8_t *) strdup ((char *) alabel);
	  if (!m)
	    return IDN2_MALLOC;

	  *insertname = m;
	}
    }
  else				/* ulabel only */
    {
      size_t ulabellen = u8_strlen (ulabel);
      uint32_t *u32;
      size_t u32len;
      size_t tmpl;
      uint8_t tmp[IDN2_LABEL_MAX_LENGTH + 1];

      if (_idn2_ascii_p (ulabel, ulabellen))
	{
	  if (ulabellen > IDN2_LABEL_MAX_LENGTH)
	    return IDN2_TOO_BIG_LABEL;

	  if (insertname)
	    {
	      uint8_t *m = (uint8_t *) strdup ((char *) ulabel);
	      if (!m)
		return IDN2_MALLOC;
	      *insertname = m;
	    }
	  return IDN2_OK;
	}

      rc = _idn2_u8_to_u32_nfc (ulabel, ulabellen, &u32, &u32len,
				flags & IDN2_NFC_INPUT);
      if (rc != IDN2_OK)
	return rc;

      rc = _idn2_label_test (TEST_NFC
			     | TEST_DISALLOWED
			     | TEST_UNASSIGNED
			     | TEST_2HYPHEN
			     | TEST_HYPHEN_STARTEND
			     | TEST_LEADING_COMBINING
			     | TEST_CONTEXTJ_RULE
			     | TEST_CONTEXTO_RULE | TEST_BIDI, u32, u32len);
      if (rc != IDN2_OK)
	{
	  free (u32);
	  return rc;
	}

      tmp[0] = 'x';
      tmp[1] = 'n';
      tmp[2] = '-';
      tmp[3] = '-';

      tmpl = IDN2_LABEL_MAX_LENGTH - 4;
      rc = idn2_punycode_encode (u32, u32len, (char *) tmp + 4, &tmpl);
      free (u32);
      if (rc != IDN2_OK)
	return rc;

      tmp[4 + tmpl] = '\0';

      if (insertname)
	{
	  uint8_t *m = (uint8_t *) strdup ((char *) tmp);
	  if (!m)
	    return IDN2_MALLOC;
	  *insertname = m;
	}
    }

  return IDN2_OK;
}

/**
 * idn2_register_ul:
 * @ulabel: input zero-terminated locale encoded string, or NULL.
 * @alabel: input zero-terminated ACE encoded string (xn--), or NULL.
 * @insertname: newly allocated output variable with name to register in DNS.
 * @flags: optional #idn2_flags to modify behaviour.
 *
 * Perform IDNA2008 register string conversion on domain label @ulabel
 * and @alabel, as described in section 4 of RFC 5891.  Note that the
 * input @ulabel is assumed to be encoded in the locale's default
 * coding system, and will be transcoded to UTF-8 and NFC normalized
 * by this function.
 *
 * It is recommended to supply both @ulabel and @alabel for better
 * error checking, but supplying just one of them will work.  Passing
 * in only @alabel is better than only @ulabel.  See RFC 5891 section
 * 4 for more information.
 *
 * After version 0.11: @insertname may be NULL to test conversion of @src
 * without allocating memory.
 *
 * Returns: On successful conversion %IDN2_OK is returned, when the
 *   given @ulabel and @alabel does not match each other
 *   %IDN2_UALABEL_MISMATCH is returned, when either of the input
 *   labels are too long %IDN2_TOO_BIG_LABEL is returned, when @alabel
 *   does does not appear to be a proper A-label %IDN2_INVALID_ALABEL
 *   is returned, when @ulabel locale to UTF-8 conversion failed
 *   %IDN2_ICONV_FAIL is returned, or another error code is returned.
 **/
int
idn2_register_ul (const char *ulabel, const char *alabel,
		  char **insertname, int flags)
{
  uint8_t *utf8ulabel = NULL;
  int rc;

  if (ulabel)
    {
      const char *encoding = locale_charset ();

      utf8ulabel = u8_strconv_from_encoding (ulabel, encoding, iconveh_error);

      if (utf8ulabel == NULL)
	{
	  if (errno == ENOMEM)
	    return IDN2_MALLOC;
	  return IDN2_ICONV_FAIL;
	}
    }

  rc = idn2_register_u8 (utf8ulabel, (const uint8_t *) alabel,
			 (uint8_t **) insertname, flags | IDN2_NFC_INPUT);

  free (utf8ulabel);

  return rc;
}
