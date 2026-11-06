/* Bidi classes of Unicode characters.
   Copyright (C) 2002, 2006, 2011-2025 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2002.

   This file is free software: you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as
   published by the Free Software Foundation; either version 2.1 of the
   License, or (at your option) any later version.

   This file is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

#include <config.h>

/* Specification.  */
#include "unictype.h"

/* Define u_bidi_category table.  */
#include "bidi_of.h"

int
uc_bidi_class (ucs4_t uc)
{
  unsigned int index1 = uc >> bidi_category_header_0;
  if (index1 < bidi_category_header_1)
    {
      int lookup1 = u_bidi_category.level1[index1];
      if (lookup1 >= 0)
        {
          unsigned int index2 = (uc >> bidi_category_header_2) & bidi_category_header_3;
          int lookup2 = u_bidi_category.level2[lookup1 + index2];
          if (lookup2 >= 0)
            {
              unsigned int index3 = ((uc & bidi_category_header_4) + lookup2) * 5;
              /* level3 contains 5-bit values, packed into 16-bit words.  */
              unsigned int lookup3 =
                (((unsigned int) u_bidi_category.level3[index3>>4]
                  | ((unsigned int) u_bidi_category.level3[(index3>>4)+1] << 16))
                 >> (index3 % 16))
                & 0x1f;

              return lookup3;
            }
        }
    }
  return UC_BIDI_L;
}

int
uc_bidi_category (ucs4_t uc)
{
  return uc_bidi_class (uc);
}
