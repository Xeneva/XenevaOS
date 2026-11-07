/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_font_style(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_FONT_STYLE_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case FONT_STYLE_NORMAL:
			value = CSS_FONT_STYLE_NORMAL;
			break;
		case FONT_STYLE_ITALIC:
			value = CSS_FONT_STYLE_ITALIC;
			break;
		case FONT_STYLE_OBLIQUE:
			value = CSS_FONT_STYLE_OBLIQUE;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_font_style(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_font_style_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_font_style(style, hint->status);
}

css_error css__initial_font_style(css_select_state *state)
{
	return set_font_style(state->computed, CSS_FONT_STYLE_NORMAL);
}

css_error css__copy_font_style(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_font_style(to, get_font_style(from));
}

css_error css__compose_font_style(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_font_style(child);

	return css__copy_font_style(
			type == CSS_FONT_STYLE_INHERIT ? parent : child,
			result);
}

