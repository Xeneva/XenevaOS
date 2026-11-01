/* bidi.c - IDNA right to left checking functions
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

#include <sys/types.h>
#include <stdbool.h>

#include "bidi.h"

#include <unictype.h>

static bool
_isBidi (const uint32_t *label, size_t llen)
{
  for (; (ssize_t) llen > 0; llen--)
    {
      int bc = uc_bidi_category (*label++);

      if (bc == UC_BIDI_R || bc == UC_BIDI_AL || bc == UC_BIDI_AN)
	return 1;
    }

  return 0;
}

/* IDNA2008 BIDI check (RFC 5893) */
int
_idn2_bidi (const uint32_t *label, size_t llen)
{
  int bc;
  int endok = 1;

  if (!_isBidi (label, llen))
    return IDN2_OK;

  // 2.1
  switch ((bc = uc_bidi_category (*label)))
    {
    case UC_BIDI_L:
      // check 2.5 & 2.6
      for (size_t it = 1; it < llen; it++)
	{
	  bc = uc_bidi_category (label[it]);

	  if (bc == UC_BIDI_L || bc == UC_BIDI_EN || bc == UC_BIDI_NSM)
	    {
	      endok = 1;
	    }
	  else
	    {
	      if (bc != UC_BIDI_ES && bc != UC_BIDI_CS && bc != UC_BIDI_ET
		  && bc != UC_BIDI_ON && bc != UC_BIDI_BN)
		{
		  /* printf("LTR label contains invalid code point\n"); */
		  return IDN2_BIDI;
		}
	      endok = 0;
	    }
	}
      /* printf("LTR label ends with invalid code point\n"); */
      return endok ? IDN2_OK : IDN2_BIDI;

    case UC_BIDI_R:
    case UC_BIDI_AL:
      // check 2.2, 2.3, 2.4
      /* printf("Label[0]=%04X: %s\n", label[0], uc_bidi_category_name(bc)); */
      for (size_t it = 1; it < llen; it++)
	{
	  bc = uc_bidi_category (label[it]);

	  /* printf("Label[%d]=%04X: %s\n", (int) it, label[it], uc_bidi_category_name(bc)); */
	  if (bc == UC_BIDI_R || bc == UC_BIDI_AL || bc == UC_BIDI_EN
	      || bc == UC_BIDI_AN || bc == UC_BIDI_NSM)
	    {
	      endok = 1;
	    }
	  else
	    {
	      if (bc != UC_BIDI_ES && bc != UC_BIDI_CS && bc != UC_BIDI_ET
		  && bc != UC_BIDI_ON && bc != UC_BIDI_BN)
		{
		  /* printf("RTL label contains invalid code point\n"); */
		  return IDN2_BIDI;
		}
	      endok = 0;
	    }
	}
      /* printf("RTL label ends with invalid code point\n"); */
      return endok ? IDN2_OK : IDN2_BIDI;

    default:
      /* printf("Label begins with invalid BIDI class %s\n", uc_bidi_category_name(bc)); */
      return IDN2_BIDI;
    }
}
