/* tr46map.c - implementation of IDNA2008 TR46 functions
   Copyright (C) 2016-2017 Tim Rühsen

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

#include <stdint.h>
#include <stdlib.h>		/* bsearch */
#include <string.h>		/* memset */

#include "tr46map.h"
#include "tr46map_data.c"

#define countof(a) (sizeof(a)/sizeof(*(a)))

static void
_fill_map (uint32_t c, const uint8_t *p, IDNAMap *map)
{
  uint32_t value;

  if (c <= 0xFF)
    {
      map->cp1 = *p++;
      map->range = *p++;
    }
  else if (c <= 0xFFFF)
    {
      map->cp1 = (p[0] << 8) | p[1];
      map->range = (p[2] << 8) | p[3];
      p += 4;
    }
  else
    {
      map->cp1 = (p[0] << 16) | (p[1] << 8) | p[2];
      map->range = (p[3] << 8) | p[4];
      p += 5;
    }

  value = (p[0] << 16) | (p[1] << 8) | p[2];

  /* deconstruct value, construction was
   *   value = (((map->nmappings << 14) | map->offset) << 3) | map->flag_index; */
  map->flag_index = value & 0x7;
  map->offset = (value >> 3) & 0x3FFF;
  map->nmappings = (value >> 17) & 0x1F;
}

static int
_compare_idna_map (const uint32_t *c, const uint8_t *p)
{
  IDNAMap map;

  _fill_map (*c, p, &map);

  if (*c < map.cp1)
    return -1;
  if (*c > map.cp1 + map.range)
    return 1;
  return 0;
}

/*
static int
_compare_idna_map(uint32_t *c, IDNAMap *m2)
{
  if (*c < m2->cp1)
    return -1;
  if (*c > m2->cp1 + m2->range)
    return 1;
  return 0;
}

IDNAMap
*get_idna_map(uint32_t c)
{
  return bsearch(&c, idna_map, countof(idna_map), sizeof(IDNAMap), (int(*)(const void *, const void *))_compare_idna_map);
}
*/

int
get_idna_map (uint32_t c, IDNAMap *map)
{
  uint8_t *p;

  if (c <= 0xFF)
    p =
      (uint8_t *) bsearch (&c, idna_map_8, sizeof (idna_map_8) / 5, 5,
			   (int (*)(const void *, const void *))
			   _compare_idna_map);
  else if (c <= 0xFFFF)
    p =
      (uint8_t *) bsearch (&c, idna_map_16, sizeof (idna_map_16) / 7, 7,
			   (int (*)(const void *, const void *))
			   _compare_idna_map);
  else if (c <= 0xFFFFFF)
    p =
      (uint8_t *) bsearch (&c, idna_map_24, sizeof (idna_map_24) / 8, 8,
			   (int (*)(const void *, const void *))
			   _compare_idna_map);
  else
    p = NULL;

  if (!p)
    {
      memset (map, 0, sizeof (IDNAMap));
      return -1;
    }

  _fill_map (c, p, map);
  return 0;
}

int
map_is (const IDNAMap *map, unsigned flags)
{
  return (idna_flags[map->flag_index] & flags) == flags;
}

static int G_GNUC_IDN2_ATTRIBUTE_PURE
_compare_nfcqc_map (uint32_t *c, NFCQCMap *m2)
{
  if (*c < m2->cp1)
    return -1;
  if (*c > m2->cp2)
    return 1;
  return 0;
}

NFCQCMap *
get_nfcqc_map (uint32_t c)
{
  return (NFCQCMap *) bsearch (&c, nfcqc_map, countof (nfcqc_map),
			       sizeof (NFCQCMap),
			       (int (*)(const void *, const void *))
			       _compare_nfcqc_map);
}

/* copy 'n' codepoints from mapdata stream */
int
get_map_data (uint32_t *dst, const IDNAMap *map)
{
  int n = map->nmappings;
  const uint8_t *src = mapdata + map->offset;

  for (; n > 0; n--)
    {
      uint32_t cp = 0;
      do
	cp = (cp << 7) | (*src & 0x7F);
      while (*src++ & 0x80);
      *dst++ = cp;
    }

  return map->nmappings;
}
