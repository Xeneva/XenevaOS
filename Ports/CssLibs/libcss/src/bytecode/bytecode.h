/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_bytecode_bytecode_h_
#define css_bytecode_bytecode_h_

#include <inttypes.h>
#include <stdio.h>

#include <libcss/types.h>
#include <libcss/properties.h>

typedef uint32_t css_code_t;

typedef enum css_properties_e opcode_t;

enum flag_value {
	FLAG_VALUE__NONE   = 0,
	FLAG_VALUE_INHERIT = 1,
	FLAG_VALUE_INITIAL = 2,
	FLAG_VALUE_REVERT  = 3,
	FLAG_VALUE_UNSET   = 4,
};

enum flag {
	FLAG_IMPORTANT = (1 << 0),
	FLAG_INHERIT   = (FLAG_VALUE_INHERIT << 1),
	FLAG_INITIAL   = (FLAG_VALUE_INITIAL << 1),
	FLAG_REVERT    = (FLAG_VALUE_REVERT  << 1),
	FLAG_UNSET     = (FLAG_VALUE_UNSET   << 1),
};


typedef enum unit {
	UNIT_LENGTH = (1u << 8),
	UNIT_PX   = (1u << 8) + 0,
	UNIT_EX   = (1u << 8) + 1,
	UNIT_EM   = (1u << 8) + 2,
	UNIT_IN   = (1u << 8) + 3,
	UNIT_CM   = (1u << 8) + 4,
	UNIT_MM   = (1u << 8) + 5,
	UNIT_PT   = (1u << 8) + 6,
	UNIT_PC   = (1u << 8) + 7,
	UNIT_CH   = (1u << 8) + 8,
	UNIT_REM  = (1u << 8) + 9,
	UNIT_LH   = (1u << 8) + 10,
	UNIT_VH   = (1u << 8) + 11,
	UNIT_VW   = (1u << 8) + 12,
	UNIT_VI   = (1u << 8) + 13,
	UNIT_VB   = (1u << 8) + 14,
	UNIT_VMIN = (1u << 8) + 15,
	UNIT_VMAX = (1u << 8) + 16,
	UNIT_Q    = (1u << 8) + 17,

	UNIT_PCT = (1 << 9),

	UNIT_ANGLE = (1 << 10),
	UNIT_DEG  = (1 << 10) + 0,
	UNIT_GRAD = (1 << 10) + 1,
	UNIT_RAD  = (1 << 10) + 2,
	UNIT_TURN = (1 << 10) + 3,

	UNIT_TIME = (1 << 11),
	UNIT_MS = (1 << 11) + 0,
	UNIT_S  = (1 << 11) + 1,

	UNIT_FREQ = (1 << 12),
	UNIT_HZ  = (1 << 12) + 0,
	UNIT_KHZ = (1 << 12) + 1,

	UNIT_RESOLUTION = (1 << 13),
	UNIT_DPI  = (1 << 13) + 0,
	UNIT_DPCM = (1 << 13) + 1,
	UNIT_DPPX = (1 << 13) + 2,
} unit;

typedef uint32_t colour;

typedef enum shape {
	SHAPE_RECT = 0
} shape;

static inline css_code_t buildOPV(opcode_t opcode, uint8_t flags, uint16_t value)
{
	return (opcode & 0x3ff) | (flags << 10) | ((value & 0x3fff) << 18);
}

static inline opcode_t getOpcode(css_code_t OPV)
{
	return (OPV & 0x3ff);
}

static inline uint8_t getFlags(css_code_t OPV)
{
	return ((OPV >> 10) & 0xff);
}

static inline uint16_t getValue(css_code_t OPV)
{
	return (OPV >> 18);
}

static inline bool isImportant(css_code_t OPV)
{
	return getFlags(OPV) & FLAG_IMPORTANT;
}

static inline enum flag_value getFlagValue(css_code_t OPV)
{
	return (getFlags(OPV) >> 1) & 0x7;
}

static inline bool hasFlagValue(css_code_t OPV)
{
	return getFlagValue(OPV) != FLAG_VALUE__NONE;
}

static inline bool isInherit(css_code_t OPV)
{
	return getFlagValue(OPV) == FLAG_VALUE_INHERIT;
}

#endif



