/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef css_select_helpers_h_
#define css_select_helpers_h_

/**
 * Convert unit bytecode to a css_unit.
 */
static inline css_unit css__to_css_unit(uint32_t u)
{
	switch (u) {
	case UNIT_PX:   return CSS_UNIT_PX;
	case UNIT_EX:   return CSS_UNIT_EX;
	case UNIT_EM:   return CSS_UNIT_EM;
	case UNIT_IN:   return CSS_UNIT_IN;
	case UNIT_CM:   return CSS_UNIT_CM;
	case UNIT_MM:   return CSS_UNIT_MM;
	case UNIT_PT:   return CSS_UNIT_PT;
	case UNIT_PC:   return CSS_UNIT_PC;
	case UNIT_CH:   return CSS_UNIT_CH;
	case UNIT_REM:  return CSS_UNIT_REM;
	case UNIT_LH:   return CSS_UNIT_LH;
	case UNIT_VH:   return CSS_UNIT_VH;
	case UNIT_VW:   return CSS_UNIT_VW;
	case UNIT_VI:   return CSS_UNIT_VI;
	case UNIT_VB:   return CSS_UNIT_VB;
	case UNIT_VMIN: return CSS_UNIT_VMIN;
	case UNIT_VMAX: return CSS_UNIT_VMAX;
	case UNIT_Q:    return CSS_UNIT_Q;
	case UNIT_PCT:  return CSS_UNIT_PCT;
	case UNIT_DEG:  return CSS_UNIT_DEG;
	case UNIT_GRAD: return CSS_UNIT_GRAD;
	case UNIT_RAD:  return CSS_UNIT_RAD;
	case UNIT_MS:   return CSS_UNIT_MS;
	case UNIT_S:    return CSS_UNIT_S;
	case UNIT_HZ:   return CSS_UNIT_HZ;
	case UNIT_KHZ:  return CSS_UNIT_KHZ;
	}

	return 0;
}

#endif
