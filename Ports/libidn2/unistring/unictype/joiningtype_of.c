/* Arabic joining type of Unicode characters.
   Copyright (C) 2011-2025 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2011.

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

/* Define u_joining_type table.  */
#include "joiningtype_of.h"

int
uc_joining_type (ucs4_t uc)
{
  unsigned int index1 = uc >> joining_type_header_0;
  if (index1 < joining_type_header_1)
    {
      int lookup1 = u_joining_type.level1[index1];
      if (lookup1 >= 0)
        {
          unsigned int index2 = (uc >> joining_type_header_2) & joining_type_header_3;
          int lookup2 = u_joining_type.level2[lookup1 + index2];
          if (lookup2 >= 0)
            {
              unsigned int index3 = (uc & joining_type_header_4) + lookup2;
              /* level3 contains 4-bit values.  */
              unsigned int lookup3 =
                (u_joining_type.level3[index3>>1] >> ((index3 % 2) * 4))
                & 0x0f;

              if (lookup3 != 0x0f)
                return lookup3;
            }
        }
    }
  if (uc_is_general_category_withtable
       (uc, UC_CATEGORY_MASK_Mn | UC_CATEGORY_MASK_Me | UC_CATEGORY_MASK_Cf))
    return UC_JOINING_TYPE_T;
  return UC_JOINING_TYPE_U;
}
