/* idna.h - internal IDNA function prototypes
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

#ifndef LIBIDN2_IDNA_H
# define LIBIDN2_IDNA_H

# include <stdint.h>
# include <stdbool.h>
# include "idn2.h"

enum
{
  TEST_NFC = 0x0001,
  TEST_2HYPHEN = 0x0002,
  TEST_HYPHEN_STARTEND = 0x0004,
  TEST_LEADING_COMBINING = 0x0008,
  TEST_DISALLOWED = 0x0010,
  /* is code point a CONTEXTJ code point? */
  TEST_CONTEXTJ = 0x0020,
  /* does code point pass CONTEXTJ rule? */
  TEST_CONTEXTJ_RULE = 0x0040,
  /* is code point a CONTEXTO code point? */
  TEST_CONTEXTO = 0x0080,
  /* is there a CONTEXTO rule for code point? */
  TEST_CONTEXTO_WITH_RULE = 0x0100,
  /* does code point pass CONTEXTO rule? */
  TEST_CONTEXTO_RULE = 0x0200,
  TEST_UNASSIGNED = 0x0400,
  TEST_BIDI = 0x0800,
  TEST_TRANSITIONAL = 0x1000,
  TEST_NONTRANSITIONAL = 0x2000,
  TEST_ALLOW_STD3_DISALLOWED = 0x4000,
};

extern int
_idn2_u8_to_u32_nfc (const uint8_t * src, size_t srclen,
		     uint32_t ** out, size_t *outlen, int nfc);

extern G_GNUC_IDN2_ATTRIBUTE_PURE bool
_idn2_ascii_p (const uint8_t * src, size_t srclen);

extern int _idn2_label_test (int what, const uint32_t * label, size_t llen);

#endif /* LIBIDN2_IDNA_H */
