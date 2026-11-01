/* context.c - check contextual rule on label
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
#include "tables.h"
#include <unictype.h>		/* uc_combining_class, UC_CCC_VR */
#include "context.h"

int
_idn2_contextj_rule (const uint32_t *label, size_t llen, size_t pos)
{
  uint32_t cp;

  if (llen == 0)
    return IDN2_OK;

  cp = label[pos];

  if (!_idn2_contextj_p (cp))
    return IDN2_OK;

  switch (cp)
    {
    case 0x200C:		/* ZERO WIDTH NON-JOINER */
      if (pos > 0)
	{
	  /* If Canonical_Combining_Class(Before(cp)) .eq.  Virama Then True; */
	  uint32_t before_cp = label[pos - 1];
	  int cc = uc_combining_class (before_cp);
	  if (cc == UC_CCC_VR)
	    return IDN2_OK;
	}

      /* See http://permalink.gmane.org/gmane.ietf.idnabis/6980 for
         clarified rule. */

      if (pos == 0 || pos == llen - 1)
	return IDN2_CONTEXTJ;

      {
	int jt;
	size_t tmp;

	/* Search backwards. */
	for (tmp = pos - 1;; tmp--)
	  {
	    jt = uc_joining_type (label[tmp]);
	    if (jt == UC_JOINING_TYPE_L || jt == UC_JOINING_TYPE_D)
	      break;
	    if (tmp == 0)
	      return IDN2_CONTEXTJ;
	    if (jt == UC_JOINING_TYPE_T)
	      continue;
	    return IDN2_CONTEXTJ;
	  }

	/* Search forward. */
	for (tmp = pos + 1; tmp < llen; tmp++)
	  {
	    jt = uc_joining_type (label[tmp]);
	    if (jt == UC_JOINING_TYPE_R || jt == UC_JOINING_TYPE_D)
	      break;
	    if (tmp == llen - 1)
	      return IDN2_CONTEXTJ;
	    if (jt == UC_JOINING_TYPE_T)
	      continue;
	    return IDN2_CONTEXTJ;
	  }
      }

      return IDN2_OK;
      break;

    case 0x200D:		/* ZERO WIDTH JOINER */
      if (pos > 0)
	{
	  uint32_t before_cp = label[pos - 1];
	  int cc = uc_combining_class (before_cp);
	  if (cc == UC_CCC_VR)
	    return IDN2_OK;
	}
      return IDN2_CONTEXTJ;
    }

  return IDN2_CONTEXTJ_NO_RULE;
}

static const char *
_uc_script_name (ucs4_t uc)
{
  const uc_script_t *ucs = uc_script (uc);

  if (!ucs)
    return "";

  return ucs->name;
}

int
_idn2_contexto_rule (const uint32_t *label, size_t llen, size_t pos)
{
  uint32_t cp = label[pos];

  if (!_idn2_contexto_p (cp))
    return IDN2_OK;

  switch (cp)
    {
    case 0x00B7:
      /* MIDDLE DOT */
      if (llen < 3)
	return IDN2_CONTEXTO;
      if (pos == 0 || pos == llen - 1)
	return IDN2_CONTEXTO;
      if (label[pos - 1] == 0x006C && label[pos + 1] == 0x006C)
	return IDN2_OK;
      return IDN2_CONTEXTO;
      break;

    case 0x0375:
      /* GREEK LOWER NUMERAL SIGN (KERAIA) */
      if (pos == llen - 1)
	return IDN2_CONTEXTO;
      if (strcmp (_uc_script_name (label[pos + 1]), "Greek") == 0)
	return IDN2_OK;
      return IDN2_CONTEXTO;
      break;

    case 0x05F3:
      /* HEBREW PUNCTUATION GERESH */
    case 0x05F4:
      /* HEBREW PUNCTUATION GERSHAYIM */
      if (pos == 0)
	return IDN2_CONTEXTO;
      if (strcmp (_uc_script_name (label[pos - 1]), "Hebrew") == 0)
	return IDN2_OK;
      return IDN2_CONTEXTO;
      break;

    case 0x0660:
    case 0x0661:
    case 0x0662:
    case 0x0663:
    case 0x0664:
    case 0x0665:
    case 0x0666:
    case 0x0667:
    case 0x0668:
    case 0x0669:
      {
	/* ARABIC-INDIC DIGITS */
	size_t i;
	for (i = 0; i < llen; i++)
	  if (label[i] >= 0x6F0 && label[i] <= 0x06F9)
	    return IDN2_CONTEXTO;
	return IDN2_OK;
	break;
      }

    case 0x06F0:
    case 0x06F1:
    case 0x06F2:
    case 0x06F3:
    case 0x06F4:
    case 0x06F5:
    case 0x06F6:
    case 0x06F7:
    case 0x06F8:
    case 0x06F9:
      {
	/* EXTENDED ARABIC-INDIC DIGITS */
	size_t i;
	for (i = 0; i < llen; i++)
	  if (label[i] >= 0x660 && label[i] <= 0x0669)
	    return IDN2_CONTEXTO;
	return IDN2_OK;
	break;
      }
    case 0x30FB:
      {
	/* KATAKANA MIDDLE DOT */
	size_t i;
	bool script_ok = false;

	for (i = 0; !script_ok && i < llen; i++)
	  if (strcmp (_uc_script_name (label[i]), "Hiragana") == 0
	      || strcmp (_uc_script_name (label[i]), "Katakana") == 0
	      || strcmp (_uc_script_name (label[i]), "Han") == 0)
	    script_ok = true;

	if (script_ok)
	  return IDN2_OK;
	return IDN2_CONTEXTO;
	break;
      }
    }

  return IDN2_CONTEXTO_NO_RULE;
}

bool
_idn2_contexto_with_rule (uint32_t cp)
{
  switch (cp)
    {
    case 0x00B7:
      /* MIDDLE DOT */
    case 0x0375:
      /* GREEK LOWER NUMERAL SIGN (KERAIA) */
    case 0x05F3:
      /* HEBREW PUNCTUATION GERESH */
    case 0x05F4:
      /* HEBREW PUNCTUATION GERSHAYIM */
    case 0x0660:
    case 0x0661:
    case 0x0662:
    case 0x0663:
    case 0x0664:
    case 0x0665:
    case 0x0666:
    case 0x0667:
    case 0x0668:
    case 0x0669:
      /* ARABIC-INDIC DIGITS */
    case 0x06F0:
    case 0x06F1:
    case 0x06F2:
    case 0x06F3:
    case 0x06F4:
    case 0x06F5:
    case 0x06F6:
    case 0x06F7:
    case 0x06F8:
    case 0x06F9:
      /* EXTENDED ARABIC-INDIC DIGITS */
    case 0x30FB:
      /* KATAKANA MIDDLE DOT */
      return true;
      break;
    }

  return false;
}
