/* error.c - libidn2 error handling helpers.
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

/* Prepare for gettext. */
#define _(x) x
#define bindtextdomain(a,b)

/**
 * idn2_strerror:
 * @rc: return code from another libidn2 function.
 *
 * Convert internal libidn2 error code to a humanly readable string.
 * The returned pointer must not be de-allocated by the caller.
 *
 * Return value: A humanly readable string describing error.
 **/
const char *
idn2_strerror (int rc)
{
  bindtextdomain (PACKAGE, LOCALEDIR);

  switch (rc)
    {
    case IDN2_OK:
      return _("success");
    case IDN2_MALLOC:
      return _("out of memory");
    case IDN2_NO_CODESET:
      return _("could not determine locale encoding format");
    case IDN2_ICONV_FAIL:
      return _("could not convert string to UTF-8");
    case IDN2_ENCODING_ERROR:
      return _("string encoding error");
    case IDN2_NFC:
      return _("string could not be NFC normalized");
    case IDN2_PUNYCODE_BAD_INPUT:
      return _("string contains invalid punycode data");
    case IDN2_PUNYCODE_BIG_OUTPUT:
      return _("punycode encoded data will be too large");
    case IDN2_PUNYCODE_OVERFLOW:
      return _("punycode conversion resulted in overflow");
    case IDN2_TOO_BIG_DOMAIN:
      return _("domain name longer than 255 characters");
    case IDN2_TOO_BIG_LABEL:
      return _("domain label longer than 63 characters");
    case IDN2_INVALID_ALABEL:
      return _("input A-label is not valid");
    case IDN2_UALABEL_MISMATCH:
      return _("input A-label and U-label does not match");
    case IDN2_NOT_NFC:
      return _("string is not in Unicode NFC format");
    case IDN2_2HYPHEN:
      return _("string contains forbidden two hyphens pattern");
    case IDN2_HYPHEN_STARTEND:
      return _("string start/ends with forbidden hyphen");
    case IDN2_LEADING_COMBINING:
      return _("string contains a forbidden leading combining character");
    case IDN2_DISALLOWED:
      return _("string contains a disallowed character");
    case IDN2_CONTEXTJ:
      return _("string contains a forbidden context-j character");
    case IDN2_CONTEXTJ_NO_RULE:
      return _("string contains a context-j character with null rule");
    case IDN2_CONTEXTO:
      return _("string contains a forbidden context-o character");
    case IDN2_CONTEXTO_NO_RULE:
      return _("string contains a context-o character with null rule");
    case IDN2_UNASSIGNED:
      return _("string contains unassigned code point");
    case IDN2_BIDI:
      return _("string has forbidden bi-directional properties");
    case IDN2_DOT_IN_LABEL:
      return _("domain label has forbidden dot (TR46)");
    case IDN2_INVALID_TRANSITIONAL:
      return
	_("domain label has character forbidden in transitional mode (TR46)");
    case IDN2_INVALID_NONTRANSITIONAL:
      return
	_
	("domain label has character forbidden in non-transitional mode (TR46)");
    case IDN2_ALABEL_ROUNDTRIP_FAILED:
      return _("A-label roundtrip failed");
    default:
      return _("Unknown error");
    }
}

#define ERR2STR(name) #name

/**
 * idn2_strerror_name:
 * @rc: return code from another libidn2 function.
 *
 * Convert internal libidn2 error code to a string corresponding to
 * internal header file symbols.  For example,
 * idn2_strerror_name(IDN2_MALLOC) will return the string
 * "IDN2_MALLOC".
 *
 * The caller must not attempt to de-allocate the returned string.
 *
 * Return value: A string corresponding to error code symbol.
 **/
const char *
idn2_strerror_name (int rc)
{
  switch (rc)
    {
    case IDN2_OK:
      return ERR2STR (IDN2_OK);
    case IDN2_MALLOC:
      return ERR2STR (IDN2_MALLOC);
    case IDN2_NO_CODESET:
      return ERR2STR (IDN2_NO_NODESET);
    case IDN2_ICONV_FAIL:
      return ERR2STR (IDN2_ICONV_FAIL);
    case IDN2_ENCODING_ERROR:
      return ERR2STR (IDN2_ENCODING_ERROR);
    case IDN2_NFC:
      return ERR2STR (IDN2_NFC);
    case IDN2_PUNYCODE_BAD_INPUT:
      return ERR2STR (IDN2_PUNYCODE_BAD_INPUT);
    case IDN2_PUNYCODE_BIG_OUTPUT:
      return ERR2STR (IDN2_PUNYCODE_BIG_OUTPUT);
    case IDN2_PUNYCODE_OVERFLOW:
      return ERR2STR (IDN2_PUNYCODE_OVERFLOW);
    case IDN2_TOO_BIG_DOMAIN:
      return ERR2STR (IDN2_TOO_BIG_DOMAIN);
    case IDN2_TOO_BIG_LABEL:
      return ERR2STR (IDN2_TOO_BIG_LABEL);
    case IDN2_INVALID_ALABEL:
      return ERR2STR (IDN2_INVALID_ALABEL);
    case IDN2_UALABEL_MISMATCH:
      return ERR2STR (IDN2_UALABEL_MISMATCH);
    case IDN2_INVALID_FLAGS:
      return ERR2STR (IDN2_INVALID_FLAGS);
    case IDN2_NOT_NFC:
      return ERR2STR (IDN2_NOT_NFC);
    case IDN2_2HYPHEN:
      return ERR2STR (IDN2_2HYPHEN);
    case IDN2_HYPHEN_STARTEND:
      return ERR2STR (IDN2_HYPHEN_STARTEND);
    case IDN2_LEADING_COMBINING:
      return ERR2STR (IDN2_LEADING_COMBINING);
    case IDN2_DISALLOWED:
      return ERR2STR (IDN2_DISALLOWED);
    case IDN2_CONTEXTJ:
      return ERR2STR (IDN2_CONTEXTJ);
    case IDN2_CONTEXTJ_NO_RULE:
      return ERR2STR (IDN2_CONTEXTJ_NO_RULE);
    case IDN2_CONTEXTO:
      return ERR2STR (IDN2_CONTEXTO);
    case IDN2_CONTEXTO_NO_RULE:
      return ERR2STR (IDN2_CONTEXTO_NO_RULE);
    case IDN2_UNASSIGNED:
      return ERR2STR (IDN2_UNASSIGNED);
    case IDN2_BIDI:
      return ERR2STR (IDN2_BIDI);
    case IDN2_DOT_IN_LABEL:
      return ERR2STR (IDN2_DOT_IN_LABEL);
    case IDN2_INVALID_TRANSITIONAL:
      return ERR2STR (IDN2_INVALID_TRANSITIONAL);
    case IDN2_INVALID_NONTRANSITIONAL:
      return ERR2STR (IDN2_INVALID_NONTRANSITIONAL);
    case IDN2_ALABEL_ROUNDTRIP_FAILED:
      return ERR2STR (IDN2_ALABEL_ROUNDTRIP_FAILED);
    default:
      return "IDN2_UNKNOWN";
    }
}
