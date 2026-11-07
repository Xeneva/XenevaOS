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

css_error css__cascade_list_style_type(uint32_t opv, css_style *style,
		css_select_state *state)
{
	uint16_t value = CSS_LIST_STYLE_TYPE_INHERIT;

	UNUSED(style);

	if (hasFlagValue(opv) == false) {
		switch (getValue(opv)) {
		case LIST_STYLE_TYPE_DISC:
			value = CSS_LIST_STYLE_TYPE_DISC;
			break;
		case LIST_STYLE_TYPE_CIRCLE:
			value = CSS_LIST_STYLE_TYPE_CIRCLE;
			break;
		case LIST_STYLE_TYPE_SQUARE:
			value = CSS_LIST_STYLE_TYPE_SQUARE;
			break;
		case LIST_STYLE_TYPE_DECIMAL:
			value = CSS_LIST_STYLE_TYPE_DECIMAL;
			break;
		case LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
			value = CSS_LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO;
			break;
		case LIST_STYLE_TYPE_LOWER_ROMAN:
			value = CSS_LIST_STYLE_TYPE_LOWER_ROMAN;
			break;
		case LIST_STYLE_TYPE_UPPER_ROMAN:
			value = CSS_LIST_STYLE_TYPE_UPPER_ROMAN;
			break;
		case LIST_STYLE_TYPE_LOWER_GREEK:
			value = CSS_LIST_STYLE_TYPE_LOWER_GREEK;
			break;
		case LIST_STYLE_TYPE_LOWER_LATIN:
			value = CSS_LIST_STYLE_TYPE_LOWER_LATIN;
			break;
		case LIST_STYLE_TYPE_UPPER_LATIN:
			value = CSS_LIST_STYLE_TYPE_UPPER_LATIN;
			break;
		case LIST_STYLE_TYPE_ARMENIAN:
			value = CSS_LIST_STYLE_TYPE_ARMENIAN;
			break;
		case LIST_STYLE_TYPE_GEORGIAN:
			value = CSS_LIST_STYLE_TYPE_GEORGIAN;
			break;
		case LIST_STYLE_TYPE_LOWER_ALPHA:
			value = CSS_LIST_STYLE_TYPE_LOWER_ALPHA;
			break;
		case LIST_STYLE_TYPE_UPPER_ALPHA:
			value = CSS_LIST_STYLE_TYPE_UPPER_ALPHA;
			break;
		case LIST_STYLE_TYPE_NONE:
			value = CSS_LIST_STYLE_TYPE_NONE;
			break;
		case LIST_STYLE_TYPE_BINARY:
			value = CSS_LIST_STYLE_TYPE_BINARY;
			break;
		case LIST_STYLE_TYPE_OCTAL:
			value = CSS_LIST_STYLE_TYPE_OCTAL;
			break;
		case LIST_STYLE_TYPE_LOWER_HEXADECIMAL:
			value = CSS_LIST_STYLE_TYPE_LOWER_HEXADECIMAL;
			break;
		case LIST_STYLE_TYPE_UPPER_HEXADECIMAL:
			value = CSS_LIST_STYLE_TYPE_UPPER_HEXADECIMAL;
			break;
		case LIST_STYLE_TYPE_ARABIC_INDIC:
			value = CSS_LIST_STYLE_TYPE_ARABIC_INDIC;
			break;
		case LIST_STYLE_TYPE_LOWER_ARMENIAN:
			value = CSS_LIST_STYLE_TYPE_LOWER_ARMENIAN;
			break;
		case LIST_STYLE_TYPE_UPPER_ARMENIAN:
			value = CSS_LIST_STYLE_TYPE_UPPER_ARMENIAN;
			break;
		case LIST_STYLE_TYPE_BENGALI:
			value = CSS_LIST_STYLE_TYPE_BENGALI;
			break;
		case LIST_STYLE_TYPE_CAMBODIAN:
			value = CSS_LIST_STYLE_TYPE_CAMBODIAN;
			break;
		case LIST_STYLE_TYPE_KHMER:
			value = CSS_LIST_STYLE_TYPE_KHMER;
			break;
		case LIST_STYLE_TYPE_CJK_DECIMAL:
			value = CSS_LIST_STYLE_TYPE_CJK_DECIMAL;
			break;
		case LIST_STYLE_TYPE_DEVANAGARI:
			value = CSS_LIST_STYLE_TYPE_DEVANAGARI;
			break;
		case LIST_STYLE_TYPE_GUJARATI:
			value = CSS_LIST_STYLE_TYPE_GUJARATI;
			break;
		case LIST_STYLE_TYPE_GURMUKHI:
			value = CSS_LIST_STYLE_TYPE_GURMUKHI;
			break;
		case LIST_STYLE_TYPE_HEBREW:
			value = CSS_LIST_STYLE_TYPE_HEBREW;
			break;
		case LIST_STYLE_TYPE_KANNADA:
			value = CSS_LIST_STYLE_TYPE_KANNADA;
			break;
		case LIST_STYLE_TYPE_LAO:
			value = CSS_LIST_STYLE_TYPE_LAO;
			break;
		case LIST_STYLE_TYPE_MALAYALAM:
			value = CSS_LIST_STYLE_TYPE_MALAYALAM;
			break;
		case LIST_STYLE_TYPE_MONGOLIAN:
			value = CSS_LIST_STYLE_TYPE_MONGOLIAN;
			break;
		case LIST_STYLE_TYPE_MYANMAR:
			value = CSS_LIST_STYLE_TYPE_MYANMAR;
			break;
		case LIST_STYLE_TYPE_ORIYA:
			value = CSS_LIST_STYLE_TYPE_ORIYA;
			break;
		case LIST_STYLE_TYPE_PERSIAN:
			value = CSS_LIST_STYLE_TYPE_PERSIAN;
			break;
		case LIST_STYLE_TYPE_TAMIL:
			value = CSS_LIST_STYLE_TYPE_TAMIL;
			break;
		case LIST_STYLE_TYPE_TELUGU:
			value = CSS_LIST_STYLE_TYPE_TELUGU;
			break;
		case LIST_STYLE_TYPE_THAI:
			value = CSS_LIST_STYLE_TYPE_THAI;
			break;
		case LIST_STYLE_TYPE_TIBETAN:
			value = CSS_LIST_STYLE_TYPE_TIBETAN;
			break;
		case LIST_STYLE_TYPE_CJK_EARTHLY_BRANCH:
			value = CSS_LIST_STYLE_TYPE_CJK_EARTHLY_BRANCH;
			break;
		case LIST_STYLE_TYPE_CJK_HEAVENLY_STEM:
			value = CSS_LIST_STYLE_TYPE_CJK_HEAVENLY_STEM;
			break;
		case LIST_STYLE_TYPE_HIAGANA:
			value = CSS_LIST_STYLE_TYPE_HIAGANA;
			break;
		case LIST_STYLE_TYPE_HIAGANA_IROHA:
			value = CSS_LIST_STYLE_TYPE_HIAGANA_IROHA;
			break;
		case LIST_STYLE_TYPE_KATAKANA:
			value = CSS_LIST_STYLE_TYPE_KATAKANA;
			break;
		case LIST_STYLE_TYPE_KATAKANA_IROHA:
			value = CSS_LIST_STYLE_TYPE_KATAKANA_IROHA;
			break;
		case LIST_STYLE_TYPE_JAPANESE_INFORMAL:
			value = CSS_LIST_STYLE_TYPE_JAPANESE_INFORMAL;
			break;
		case LIST_STYLE_TYPE_JAPANESE_FORMAL:
			value = CSS_LIST_STYLE_TYPE_JAPANESE_FORMAL;
			break;
		case LIST_STYLE_TYPE_KOREAN_HANGUL_FORMAL:
			value = CSS_LIST_STYLE_TYPE_KOREAN_HANGUL_FORMAL;
			break;
		case LIST_STYLE_TYPE_KOREAN_HANJA_INFORMAL:
			value = CSS_LIST_STYLE_TYPE_KOREAN_HANJA_INFORMAL;
			break;
		case LIST_STYLE_TYPE_KOREAN_HANJA_FORMAL:
			value = CSS_LIST_STYLE_TYPE_KOREAN_HANJA_FORMAL;
			break;
		}
	}

	if (css__outranks_existing(getOpcode(opv), isImportant(opv), state,
			getFlagValue(opv))) {
		return set_list_style_type(state->computed, value);
	}

	return CSS_OK;
}

css_error css__set_list_style_type_from_hint(const css_hint *hint,
		css_computed_style *style)
{
	return set_list_style_type(style, hint->status);
}

css_error css__initial_list_style_type(css_select_state *state)
{
	return set_list_style_type(state->computed, CSS_LIST_STYLE_TYPE_DISC);
}

css_error css__copy_list_style_type(
		const css_computed_style *from,
		css_computed_style *to)
{
	if (from == to) {
		return CSS_OK;
	}

	return set_list_style_type(to, get_list_style_type(from));
}

css_error css__compose_list_style_type(const css_computed_style *parent,
		const css_computed_style *child,
		css_computed_style *result)
{
	uint8_t type = get_list_style_type(child);

	return css__copy_list_style_type(
			type == CSS_LIST_STYLE_TYPE_INHERIT ? parent : child,
			result);
}
