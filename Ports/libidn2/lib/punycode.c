/* punycode.c - punycode encoding/decoding
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

/*
  Code copied from http://www.nicemice.net/idn/punycode-spec.gz on
  2011-01-04 with SHA-1 a966a8017f6be579d74a50a226accc7607c40133
  labeled punycode-spec 1.0.3 (2006-Mar-23-Thu).  It is modified for
  Libidn2 by Simon Josefsson and others.  Original code license:

  punycode-spec 1.0.3 (2006-Mar-23-Thu)
  http://www.nicemice.net/idn/
  Adam M. Costello
  http://www.nicemice.net/amc/

  B. Disclaimer and license

    Regarding this entire document or any portion of it (including
    the pseudocode and C code), the author makes no guarantees and
    is not responsible for any damage resulting from its use.  The
    author grants irrevocable permission to anyone to use, modify,
    and distribute it in any way that does not diminish the rights
    of anyone else to use, modify, and distribute it, provided that
    redistributed derivative works do not contain misleading author or
    version information.  Derivative works need not be licensed under
    similar terms.

  C. Punycode sample implementation

  punycode-sample.c 2.0.0 (2004-Mar-21-Sun)
  http://www.nicemice.net/idn/
  Adam M. Costello
  http://www.nicemice.net/amc/

  This is ANSI C code (C89) implementing Punycode 1.0.x.
*/

#include <config.h>

#include "idn2.h"		/* IDN2_OK, ... */

#include <string.h>

/*** Bootstring parameters for Punycode ***/

enum
{ base = 36, tmin = 1, tmax = 26, skew = 38, damp = 700,
  initial_bias = 72, initial_n = 0x80, delimiter = 0x2D
};

/* basic(cp) tests whether cp is a basic code point: */
#define encode_basic(cp) ((uint32_t)(cp) < 0x80)

/* encode_digit(d,flag) returns the basic code point whose value      */
/* (when used for representing integers) is d, which needs to be in   */
/* the range 0 to base-1.  The lowercase form is used unless flag is  */
/* nonzero, in which case the uppercase form is used.  The behavior   */
/* is undefined if flag is nonzero and digit d has no uppercase form. */

static char
encode_digit (uint32_t d, int flag)
{
  return d + 22 + 75 * (d < 26) - ((flag != 0) << 5);
  /*  0..25 map to ASCII a..z or A..Z */
  /* 26..35 map to ASCII 0..9         */
}

/* basic(cp) tests whether cp is a basic code point: */
#define decode_basic(cp)			       \
  ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <='9')		\
   || (cp >= 'A' && cp <='Z') || cp == '-' || cp == '_')

/* decode_digit(cp) returns the numeric value of a basic code */
/* point (for use in representing integers) in the range 0 to */
/* base-1, or base if cp does not represent a value.          */

static unsigned
decode_digit (int cp)
{
  if (cp >= 'a' && cp <= 'z')
    return cp - 'a';
  if (cp >= '0' && cp <= '9')
    return cp - '0' + 26;
  if (cp >= 'A' && cp <= 'Z')
    return cp - 'A';

  return 0;
}

/*** Bias adaptation function ***/

static uint32_t
adapt (uint32_t delta, uint32_t numpoints, int firsttime)
  _GL_ATTRIBUTE_CONST;

     static uint32_t adapt (uint32_t delta, uint32_t numpoints, int firsttime)
{
  uint32_t k;

  delta = firsttime ? delta / damp : delta >> 1;
  /* delta >> 1 is a faster way of doing delta / 2 */
  delta += delta / numpoints;

  for (k = 0; delta > ((base - tmin) * tmax) / 2; k += base)
    {
      delta /= base - tmin;
    }

  return k + (base - tmin + 1) * delta / (delta + skew);
}

/**
 * idn2_punycode_encode:
 * @input: array of input Unicode (UCS-4) code points
 * @input_length: number of code points in the @input array.
 * @output: output character array with ASCII code points.
 * @output_length: size of @output array
 *
 * Converts a sequence of code points to Punycode.
 *
 * Warning: Be aware that is rare for applications to need to perform
 *   Punycode operations, and you should consider idn2_to_ascii_8z(),
 *   idn2_to_ascii_lz(), idn2_to_ascii_4z() etc.
 *
 * Returns: On successful encoding %IDN2_OK is returned, or an error
 *   codes like %IDN2_PUNYCODE_BAD_INPUT, %IDN2_PUNYCODE_BIG_OUTPUT,
 *   or %IDN2_PUNYCODE_OVERFLOW.
 *
 * Since: 2.3.5
 **/
int
idn2_punycode_encode (const uint32_t *input,
		      size_t input_length,
		      char *output, size_t *output_length)
{
  uint32_t our_input_length, n, delta, h, b, bias, j, m, q, k, t;
  size_t out, max_out;

  /* The Punycode spec assumes that the input length is the same type */
  /* of integer as a code point, so we need to convert the size_t to  */
  /* a uint32_t, which could overflow.                           */

  if (input_length > UINT32_MAX)
    return IDN2_PUNYCODE_OVERFLOW;
  our_input_length = (uint32_t) input_length;

  /* Initialize the state: */

  n = initial_n;
  delta = 0;
  out = 0;
  max_out = *output_length;
  bias = initial_bias;

  /* Handle the basic code points: */

  for (j = 0; j < our_input_length; ++j)
    {
      if (encode_basic (input[j]))
	{
	  if (max_out - out < 2)
	    return IDN2_PUNYCODE_BIG_OUTPUT;
	  output[out++] = (char) input[j];
	}
      else if (input[j] > 0x10FFFF
	       || (input[j] >= 0xD800 && input[j] <= 0xDBFF))
	return IDN2_PUNYCODE_BAD_INPUT;
    }

  h = b = (uint32_t) out;
  /* cannot overflow because out <= our_input_length <= UINT32_MAX */

  /* h is the number of code points that have been handled, b is the  */
  /* number of basic code points, and out is the number of ASCII code */
  /* points that have been output.                                    */

  if (b > 0)
    output[out++] = delimiter;

  /* Main encoding loop: */

  while (h < our_input_length)
    {
      /* All non-basic code points < n have been     */
      /* handled already.  Find the next larger one: */

      for (m = UINT32_MAX, j = 0; j < our_input_length; ++j)
	{
	  /* if (basic(input[j])) continue; */
	  /* (not needed for Punycode) */
	  if (input[j] >= n && input[j] < m)
	    m = input[j];
	}

      /* Increase delta enough to advance the decoder's    */
      /* <n,i> state to <m,0>, but guard against overflow: */

      if (m - n > (UINT32_MAX - delta) / (h + 1))
	return IDN2_PUNYCODE_OVERFLOW;
      delta += (m - n) * (h + 1);
      n = m;

      for (j = 0; j < our_input_length; ++j)
	{
	  /* Punycode does not need to check whether input[j] is basic: */
	  if (input[j] < n /* || basic(input[j]) */ )
	    {
	      if (++delta == 0)
		return IDN2_PUNYCODE_OVERFLOW;
	    }

	  if (input[j] == n)
	    {
	      /* Represent delta as a generalized variable-length integer: */

	      for (q = delta, k = base;; k += base)
		{
		  if (out >= max_out)
		    return IDN2_PUNYCODE_BIG_OUTPUT;
		  t = k <= bias /* + tmin */ ? tmin :	/* +tmin not needed */
		    k >= bias + tmax ? tmax : k - bias;
		  if (q < t)
		    break;
		  output[out++] = encode_digit (t + (q - t) % (base - t), 0);
		  q = (q - t) / (base - t);
		}

	      output[out++] = encode_digit (q, 0);
	      bias = adapt (delta, h + 1, h == b);
	      delta = 0;
	      ++h;
	    }
	}

      ++delta, ++n;
    }

  *output_length = out;
  return IDN2_OK;
}

/**
 * idn2_punycode_decode:
 * @input: array of ASCII characters (0..7F)
 * @input_length: Number of ASCII characters in the @input array.
 * @output: output character array with ASCII code points.
 * @output_length: on input, guarantee that @output has room
 *   for this many code points; on output, *@output_length
 *   holds number of code points in @output.
 *
 * Converts Punycode to a sequence of code points.
 *
 * The decoder will never need to output more code points than the
 * number of ASCII code points in the input, because of the way the
 * encoding is defined.
 *
 * The number of code points output cannot exceed the maximum possible
 * value of a uint32_t, even if the supplied @output_length is greater
 * than that.
 *
 * Warning: Be aware that is rare for applications to need to perform
 *   Punycode operations, and you should consider
 *   idn2_to_unicode_8z8z(), idn2_to_unicode_lzlz() etc.
 *
 * Returns: On successful encoding %IDN2_OK is returned, or an error
 *   codes like %IDN2_PUNYCODE_BAD_INPUT, %IDN2_PUNYCODE_BIG_OUTPUT,
 *   or %IDN2_PUNYCODE_OVERFLOW.
 *
 * Since: 2.3.5
 **/
int
idn2_punycode_decode (const char *input,
		      size_t input_length,
		      uint32_t *output, size_t *output_length)
{
  uint32_t n, out = 0, i, max_out, bias, oldi, w, k, digit, t;
  size_t b = 0, j, in;

  if (!input_length)
    return IDN2_PUNYCODE_BAD_INPUT;

  /* Check that all chars are basic */
  for (j = 0; j < input_length; ++j)
    {
      if (!decode_basic (input[j]))
	return IDN2_PUNYCODE_BAD_INPUT;
      if (input[j] == delimiter)
	b = j;
    }

  max_out =
    *output_length > UINT32_MAX ? UINT32_MAX : (uint32_t) * output_length;

  if (input[b] == delimiter)
    {
      /* do not accept leading or trailing delimiter
       *   - leading delim must be omitted if there is no ASCII char in u-label
       *   - trailing delim means there where no non-ASCII chars in u-label
       */
      if (!b || b == input_length - 1)
	return IDN2_PUNYCODE_BAD_INPUT;

      if (b >= max_out)
	return IDN2_PUNYCODE_BIG_OUTPUT;

      /* Check that all chars before last delimiter are basic chars */
      /* and copy the first b code points to the output. */
      for (j = 0; j < b; j++)
	output[out++] = input[j];

      b += 1;			/* advance to non-basic char encoding */
    }

  /* Initialize the state: */
  n = initial_n;
  i = 0;
  bias = initial_bias;

  /* Main decoding loop:  Start just after the last delimiter if any  */
  /* basic code points were copied; start at the beginning otherwise. */
  for (in = b; in < input_length; ++out)
    {

      /* in is the index of the next ASCII code point to be consumed, */
      /* and out is the number of code points in the output array.    */

      /* Decode a generalized variable-length integer into delta,  */
      /* which gets added to i.  The overflow checking is easier   */
      /* if we increase i as we go, then subtract off its starting */
      /* value at the end to obtain delta.                         */
      for (oldi = i, w = 1, k = base;; k += base)
	{
	  if (in >= input_length)
	    return IDN2_PUNYCODE_BAD_INPUT;
	  digit = decode_digit (input[in++]);
	  if (digit >= base)
	    return IDN2_PUNYCODE_BAD_INPUT;
	  if (digit > (UINT32_MAX - i) / w)
	    return IDN2_PUNYCODE_OVERFLOW;
	  i += digit * w;
	  t = k <= bias /* + tmin */ ? tmin :	/* +tmin not needed */
	    k >= bias + tmax ? tmax : k - bias;
	  if (digit < t)
	    break;
	  if (w > UINT32_MAX / (base - t))
	    return IDN2_PUNYCODE_OVERFLOW;
	  w *= (base - t);
	}

      bias = adapt (i - oldi, out + 1, oldi == 0);

      /* i was supposed to wrap around from out+1 to 0,   */
      /* incrementing n each time, so we'll fix that now: */
      if (i / (out + 1) > UINT32_MAX - n)
	return IDN2_PUNYCODE_OVERFLOW;
      n += i / (out + 1);
      if (n > 0x10FFFF || (n >= 0xD800 && n <= 0xDBFF))
	return IDN2_PUNYCODE_BAD_INPUT;
      i %= (out + 1);

      /* Insert n at position i of the output: */

      /* not needed for Punycode: */
      /* if (basic(n)) return IDN2_PUNYCODE_BAD_INPUT; */
      if (out >= max_out)
	return IDN2_PUNYCODE_BIG_OUTPUT;

      memmove (output + i + 1, output + i, (out - i) * sizeof *output);
      output[i++] = n;
    }

  *output_length = (size_t) out;
  /* cannot overflow because out <= old value of *output_length */
  return IDN2_OK;
}

/* We are stuck exporting these old internal interfaces because old
   versions of GNUTLS used them, and maybe other software. */

extern int _IDN2_API _idn2_punycode_decode (size_t input_length,
					    const char input[],
					    size_t *output_length,
					    uint32_t output[]);
extern int _IDN2_API _idn2_punycode_encode (size_t input_length,
					    const uint32_t input[],
					    size_t *output_length,
					    char output[]);

int
_idn2_punycode_decode (size_t input_length,
		       const char input[],
		       size_t *output_length, uint32_t output[])
{
  return idn2_punycode_decode (input, input_length, output, output_length);

}

int
_idn2_punycode_encode (size_t input_length,
		       const uint32_t input[],
		       size_t *output_length, char output[])
{
  return idn2_punycode_encode (input, input_length, output, output_length);
}
