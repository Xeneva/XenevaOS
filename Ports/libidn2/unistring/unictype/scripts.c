/* Scripts of Unicode characters.
   Copyright (C) 2007, 2009-2025 Free Software Foundation, Inc.
   Written by Bruno Haible <bruno@clisp.org>, 2007.

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

#include <string.h>

#include "scripts.h"
#include "unictype/scripts_byname.h"

const uc_script_t *
uc_script (ucs4_t uc)
{
  unsigned int index1 = uc >> script_header_0;
  if (index1 < script_header_1)
    {
      int lookup1 = u_script.level1[index1];
      if (lookup1 >= 0)
        {
          unsigned int index2 = (uc >> script_header_2) & script_header_3;
          unsigned int lookup2 = u_script.level2[lookup1 + index2];
          if (lookup2 > 0)
            {
              unsigned int index3 = (uc & script_header_4);
              unsigned char lookup3 = u_script.level3[(lookup2 - 1) + index3];

              if (lookup3 != 0xff)
                return &scripts[lookup3];
            }
        }
    }
  return NULL;
}

const uc_script_t *
uc_script_byname (const char *script_name)
{
  const struct named_script *found;

  found = uc_script_lookup (script_name, strlen (script_name));
  if (found != NULL)
    return &scripts[found->index];
  else
    return NULL;
}

bool
uc_is_script (ucs4_t uc, const uc_script_t *script)
{
  return uc_script (uc) == script;
}

void
uc_all_scripts (const uc_script_t **scriptsp, size_t *countp)
{
  *scriptsp = scripts;
  *countp = sizeof (scripts) / sizeof (scripts[0]);
}
