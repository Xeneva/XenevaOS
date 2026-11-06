/* tr46map.h - header file for IDNA2008 TR46
   Copyright (C) 2016-2025 Tim Ruehsen

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

#ifndef LIBIDN2_TR46MAP_H
# define LIBIDN2_TR46MAP_H

# include <stdint.h>
# include "idn2.h"

# define TR46_FLG_VALID                   1
# define TR46_FLG_MAPPED                  2
# define TR46_FLG_IGNORED                 4
# define TR46_FLG_DEVIATION               8
# define TR46_FLG_DISALLOWED             16
# define TR46_FLG_DISALLOWED_STD3_MAPPED 32
# define TR46_FLG_DISALLOWED_STD3_VALID  64

typedef struct
{
  uint32_t cp1;
  uint16_t range;
  unsigned nmappings:5,		/* 0-18, # of uint32_t at <offset> */
    offset:14,			/* 0-16383, byte offset into mapdata */
    flag_index:3;		/* 0-7, index into flags */
} IDNAMap;

typedef struct
{
  uint32_t cp1, cp2;
  char check;			/* 0=NO 2=MAYBE (YES if codepoint has no table entry) */
} NFCQCMap;

int get_idna_map (uint32_t c, IDNAMap * map);
int get_map_data (uint32_t * dst, const IDNAMap * map);
int G_GNUC_IDN2_ATTRIBUTE_PURE map_is (const IDNAMap * map, unsigned flags);

G_GNUC_IDN2_ATTRIBUTE_PURE NFCQCMap *get_nfcqc_map (uint32_t c);

#endif /* LIBIDN2_TR46MAP_H */
