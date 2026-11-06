/* free.c - implement stub free() caller, typically for Windows
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

#include <stdlib.h>		/* free */

/**
 * idn2_free:
 * @ptr: pointer to deallocate
 *
 * Call free(3) on the given pointer.
 *
 * This function is typically only useful on systems where the library
 * malloc heap is different from the library caller malloc heap, which
 * happens on Windows when the library is a separate DLL.
 **/
void
idn2_free (void *ptr)
{
  free (ptr);
}
