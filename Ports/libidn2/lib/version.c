/* version.c - implementation of version checking functions
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

#include <string.h>		/* strverscmp */
#include <version-etc.h>	/* version_etc_copyright */

#ifdef __cplusplus
extern				// define a global const variable in C++, C doesn't need it.
#endif
const char version_etc_copyright[] =
  /* Do *not* mark this string for translation */
  "Copyright (C) 2011-2025 Simon Josefsson";

/**
 * idn2_check_version:
 * @req_version: version string to compare with, or NULL.
 *
 * Check IDN2 library version.  This function can also be used to read
 * out the version of the library code used.  See %IDN2_VERSION for a
 * suitable @req_version string, it corresponds to the idn2.h header
 * file version.  Normally these two version numbers match, but if you
 * are using an application built against an older libidn2 with a
 * newer libidn2 shared library they will be different.
 *
 * Return value: Check that the version of the library is at
 *   minimum the one given as a string in @req_version and return the
 *   actual version string of the library; return NULL if the
 *   condition is not met.  If NULL is passed to this function no
 *   check is done and only the version string is returned.
 **/
const char *
idn2_check_version (const char *req_version)
{
  if (!req_version || strverscmp (req_version, IDN2_VERSION) <= 0)
    return IDN2_VERSION;

  return NULL;
}
