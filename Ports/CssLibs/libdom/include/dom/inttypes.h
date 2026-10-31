/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2015 Vincent Sanders <vince@netsurf-browser.org>
 */

#ifndef dom_inttypes_h_
#define dom_inttypes_h_

#include <stdint.h>

/**
 * The IDL spec(2nd ed) 3.10.5 defines a short type with 16bit range
 */
typedef int16_t dom_short;

/**
 * The IDL spec(2nd ed) 3.10.6 defines an unsigned short type with 16bit range
 */
typedef uint16_t dom_ushort;

/**
 * The IDL spec(2nd ed) 3.10.7 defines a long type with 32bit range
 */
typedef int32_t dom_long;

/**
 * The IDL spec(2nd ed) 3.10.8 defines an unsigned long type with 32bit range
 */
typedef uint32_t dom_ulong;

#endif
