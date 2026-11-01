/* idna.c - implementation of high-level IDNA processing function
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

#include <stdlib.h>		/* free */
#include <errno.h>		/* errno */

#include "idn2.h"
#include "bidi.h"
#include "tables.h"
#include "context.h"
#include "tr46map.h"

#include <unitypes.h>
#include <unictype.h>		/* uc_is_general_category, UC_CATEGORY_M */
#include <uninorm.h>		/* u32_normalize */
#include <unistr.h>		/* u8_to_u32 */

#include "idna.h"

/*
 * NFC Quick Check from
 * http://unicode.org/reports/tr15/#Detecting_Normalization_Forms
 *
 * They say, this is much faster than 'brute force' normalization.
 * Strings are very likely already in NFC form.
 */
G_GNUC_IDN2_ATTRIBUTE_PURE static int
_isNFC (uint32_t *label, size_t len)
{
  int lastCanonicalClass = 0;
  int result = 1;
  size_t it;

  for (it = 0; it < len; it++)
    {
      uint32_t ch = label[it];

      // supplementary code point
      if (ch >= 0x10000)
	it++;

      int canonicalClass = uc_combining_class (ch);
      if (lastCanonicalClass > canonicalClass && canonicalClass != 0)
	return 0;

      NFCQCMap *map = get_nfcqc_map (ch);
      if (map)
	{
	  if (map->check)
	    return 0;
	  result = -1;
	}

      lastCanonicalClass = canonicalClass;
    }

  return result;
}

int
_idn2_u8_to_u32_nfc (const uint8_t *src, size_t srclen,
		     uint32_t **out, size_t *outlen, int nfc)
{
  uint32_t *p;
  size_t plen;

  p = u8_to_u32 (src, srclen, NULL, &plen);
  if (p == NULL)
    {
      if (errno == ENOMEM)
	return IDN2_MALLOC;
      return IDN2_ENCODING_ERROR;
    }

  if (nfc && !_isNFC (p, plen))
    {
      size_t tmplen;
      uint32_t *tmp = u32_normalize (UNINORM_NFC, p, plen, NULL, &tmplen);
      free (p);
      if (tmp == NULL)
	{
	  if (errno == ENOMEM)
	    return IDN2_MALLOC;
	  return IDN2_NFC;
	}

      p = tmp;
      plen = tmplen;
    }

  *out = p;
  *outlen = plen;
  return IDN2_OK;
}

bool
_idn2_ascii_p (const uint8_t *src, size_t srclen)
{
  size_t i;

  for (i = 0; i < srclen; i++)
    if (src[i] >= 0x80)
      return false;

  return true;
}

int
_idn2_label_test (int what, const uint32_t *label, size_t llen)
{
  if (what & TEST_NFC)
    {
      size_t plen;
      uint32_t *p = u32_normalize (UNINORM_NFC, label, llen,
				   NULL, &plen);
      int ok;
      if (p == NULL)
	{
	  if (errno == ENOMEM)
	    return IDN2_MALLOC;
	  return IDN2_NFC;
	}
      ok = llen == plen && memcmp (label, p, plen * sizeof (*label)) == 0;
      free (p);
      if (!ok)
	return IDN2_NOT_NFC;
    }

  if (what & TEST_2HYPHEN)
    {
      if (llen >= 4 && label[2] == '-' && label[3] == '-')
	return IDN2_2HYPHEN;
    }

  if (what & TEST_HYPHEN_STARTEND)
    {
      if (llen > 0 && (label[0] == '-' || label[llen - 1] == '-'))
	return IDN2_HYPHEN_STARTEND;
    }

  if (what & TEST_LEADING_COMBINING)
    {
      if (llen > 0 && uc_is_general_category (label[0], UC_CATEGORY_M))
	return IDN2_LEADING_COMBINING;
    }

  if (what & TEST_DISALLOWED)
    {
      size_t i;
      for (i = 0; i < llen; i++)
	if (_idn2_disallowed_p (label[i]))
	  {
	    if ((what & (TEST_TRANSITIONAL | TEST_NONTRANSITIONAL)) &&
		(what & TEST_ALLOW_STD3_DISALLOWED))
	      {
		IDNAMap map;
		get_idna_map (label[i], &map);
		if (map_is (&map, TR46_FLG_DISALLOWED_STD3_VALID) ||
		    map_is (&map, TR46_FLG_DISALLOWED_STD3_MAPPED))
		  continue;

	      }

	    return IDN2_DISALLOWED;
	  }
    }

  if (what & TEST_CONTEXTJ)
    {
      size_t i;
      for (i = 0; i < llen; i++)
	if (_idn2_contextj_p (label[i]))
	  return IDN2_CONTEXTJ;
    }

  if (what & TEST_CONTEXTJ_RULE)
    {
      size_t i;
      int rc;

      for (i = 0; i < llen; i++)
	{
	  rc = _idn2_contextj_rule (label, llen, i);
	  if (rc != IDN2_OK)
	    return rc;
	}
    }

  if (what & TEST_CONTEXTO)
    {
      size_t i;
      for (i = 0; i < llen; i++)
	if (_idn2_contexto_p (label[i]))
	  return IDN2_CONTEXTO;
    }

  if (what & TEST_CONTEXTO_WITH_RULE)
    {
      size_t i;
      for (i = 0; i < llen; i++)
	if (_idn2_contexto_p (label[i])
	    && !_idn2_contexto_with_rule (label[i]))
	  return IDN2_CONTEXTO_NO_RULE;
    }

  if (what & TEST_CONTEXTO_RULE)
    {
      size_t i;
      int rc;

      for (i = 0; i < llen; i++)
	{
	  rc = _idn2_contexto_rule (label, llen, i);
	  if (rc != IDN2_OK)
	    return rc;
	}
    }

  if (what & TEST_UNASSIGNED)
    {
      size_t i;
      for (i = 0; i < llen; i++)
	if (_idn2_unassigned_p (label[i]))
	  return IDN2_UNASSIGNED;
    }

  if (what & TEST_BIDI)
    {
      int rc = _idn2_bidi (label, llen);
      if (rc != IDN2_OK)
	return rc;
    }

  if (what & (TEST_TRANSITIONAL | TEST_NONTRANSITIONAL))
    {
      size_t i;
      int transitional = what & TEST_TRANSITIONAL;

      /* TR46: 4. The label must not contain a U+002E ( . ) FULL STOP */
      for (i = 0; i < llen; i++)
	if (label[i] == 0x002E)
	  return IDN2_DOT_IN_LABEL;

      /* TR46: 6. Each code point in the label must only have certain status
       * values according to Section 5, IDNA Mapping Table:
       *    a. For Transitional Processing, each value must be valid.
       *    b. For Nontransitional Processing, each value must be either valid or deviation. */
      for (i = 0; i < llen; i++)
	{
	  IDNAMap map;

	  get_idna_map (label[i], &map);

	  if (map_is (&map, TR46_FLG_VALID) ||
	      (!transitional && map_is (&map, TR46_FLG_DEVIATION)))
	    continue;

	  if (what & TEST_ALLOW_STD3_DISALLOWED &&
	      (map_is (&map, TR46_FLG_DISALLOWED_STD3_VALID) ||
	       map_is (&map, TR46_FLG_DISALLOWED_STD3_MAPPED)))
	    continue;

	  return transitional ? IDN2_INVALID_TRANSITIONAL :
	    IDN2_INVALID_NONTRANSITIONAL;
	}
    }

  return IDN2_OK;
}
