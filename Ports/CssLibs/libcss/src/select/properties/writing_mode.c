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

css_error css__cascade_writing_mode(uint32_t opv, css_style *style,
		css_select_state *state)
{
	enum flag_value flag_value = getFlagValue(opv);
	uint16_t writing_mode = CSS_WRITING_MODE_INHERIT;
	UNUSED(style);

	if (flag_value == FLAG_VALUE__NONE) {
		switch (getValue(opv)) {
		case WRITING_MODE_HORIZONTAL_TB:
			writing_mode = CSS_WRITING_MODE_HORIZONTAL_TB;
			break;
		case WRITING_MODE_VERTICAL_RL:
			writing_mode = CSS_WRITING_MODE_VERTICAL_RL;
			break;
		case WRITING_MODE_VERTICAL_LR:
			writing_mode = CSS_WRITING_MODE_VERTICAL_LR;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			flag_value)) {
		return set_writing_mode(state->computed, writing_mode);
	}

	return CSS_OK;
}

css_error css__set_writing_mode_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_writing_mode(style, hint->status);
}

css_error css__initial_writing_mode(css_select_state *state)
{
	return set_writing_mode(state->computed,
			CSS_WRITING_MODE_HORIZONTAL_TB);
}

css_error css__copy_writing_mode(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_writing_mode(to, get_writing_mode(from));
}

css_error css__compose_writing_mode(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_writing_mode(child);

	return css__copy_writing_mode(
			type == CSS_WRITING_MODE_INHERIT ? parent : child,
			result);
}

