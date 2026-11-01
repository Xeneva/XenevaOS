/* tables.c - IDNA table checking functions
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

#include "tables.h"

#include <stdlib.h>		/* bsearch */

#include "data.h"

static int
_compare (const struct idna_table *m1, const struct idna_table *m2)
{
  if (m1->start < m2->start)
    return -1;
  if (m1->start > m2->end)
    return 1;
  return 0;
}

static int
property (uint32_t cp)
  _GL_ATTRIBUTE_CONST;

     static int property (uint32_t cp)
{
  const struct idna_table *result;
  struct idna_table key;

  key.start = cp;

  result = (struct idna_table *)
    bsearch (&key, idna_table, idna_table_size,
	     sizeof (struct idna_table),
	     (int (*)(const void *, const void *)) _compare);

  return result ? result->state : UNASSIGNED;
}

int
_idn2_disallowed_p (uint32_t cp)
{
  return property (cp) == DISALLOWED;
}

int
_idn2_contextj_p (uint32_t cp)
{
  return property (cp) == CONTEXTJ;
}

int
_idn2_contexto_p (uint32_t cp)
{
  return property (cp) == CONTEXTO;
}

int
_idn2_unassigned_p (uint32_t cp)
{
  return property (cp) == UNASSIGNED;
}
