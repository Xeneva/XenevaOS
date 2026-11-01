/* context.h - check contextual rule on label
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

#ifndef LIBIDN2_CONTEXT_H
# define LIBIDN2_CONTEXT_H

# include <stdint.h>
# include <stdbool.h>
# include "idn2.h"

int G_GNUC_IDN2_ATTRIBUTE_PURE
_idn2_contextj_rule (const uint32_t * label, size_t llen, size_t pos);

int G_GNUC_IDN2_ATTRIBUTE_PURE
_idn2_contexto_rule (const uint32_t * label, size_t llen, size_t pos);

bool G_GNUC_IDN2_ATTRIBUTE_CONST _idn2_contexto_with_rule (uint32_t cp);

#endif /* LIBIDN2_CONTEXT_H */
