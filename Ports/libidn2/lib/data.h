/* data.h - IDNA tables
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

#ifndef LIBIDN2_DATA_H
# define LIBIDN2_DATA_H

# include <stdint.h>
# include <sys/types.h>

enum
{
  PVALID, CONTEXTJ, CONTEXTO, DISALLOWED, UNASSIGNED
};

struct idna_table
{
  uint32_t start;
  uint32_t end;
  int state;
};

extern const struct idna_table idna_table[];
extern const size_t idna_table_size;

#endif /* LIBIDN2_DATA_H */
