/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "select/propset.h"
#include "select/propget.h"
#include "utils/utils.h"

#include "select/properties/properties.h"
#include "select/properties/helpers.h"

css_error css__cascade_text_decoration(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_TEXT_DECORATION_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		if (getValue(opv) == TEXT_DECORATION_NONE) {
			value = CSS_TEXT_DECORATION_NONE;
		} else {
			assert(value == 0);

			if (getValue(opv) & TEXT_DECORATION_UNDERLINE)
				value |= CSS_TEXT_DECORATION_UNDERLINE;
			if (getValue(opv) & TEXT_DECORATION_OVERLINE)
				value |= CSS_TEXT_DECORATION_OVERLINE;
			if (getValue(opv) & TEXT_DECORATION_LINE_THROUGH)
				value |= CSS_TEXT_DECORATION_LINE_THROUGH;
			if (getValue(opv) & TEXT_DECORATION_BLINK)
				value |= CSS_TEXT_DECORATION_BLINK;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_text_decoration(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_text_decoration_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_text_decoration(style, hint->status);
}

css_error css__initial_text_decoration(css_select_state *state)
{
	return set_text_decoration(state->computed, CSS_TEXT_DECORATION_NONE);
}

css_error css__copy_text_decoration(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_text_decoration(to, get_text_decoration(from));
}

css_error css__compose_text_decoration(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_text_decoration(child);

	return css__copy_text_decoration(
			type == CSS_TEXT_DECORATION_INHERIT ? parent : child,
			result);
}

