/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <libwapcaplet/libwapcaplet.h>

#include "select/strings.h"
#include "utils/utils.h"

css_error css_select_strings_intern(css_select_strings *str)
{
	lwc_error error;

	/* Universal selector */
	error = lwc_intern_string("*", SLEN("*"), &str->universal);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	/* Pseudo classes */
	error = lwc_intern_string(
			"first-child", SLEN("first-child"),
			&str->first_child);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"link", SLEN("link"),
			&str->link);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"visited", SLEN("visited"),
			&str->visited);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"hover", SLEN("hover"),
			&str->hover);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"active", SLEN("active"),
			&str->active);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"focus", SLEN("focus"),
			&str->focus);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"nth-child", SLEN("nth-child"),
			&str->nth_child);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"nth-last-child", SLEN("nth-last-child"),
			&str->nth_last_child);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"nth-of-type", SLEN("nth-of-type"),
			&str->nth_of_type);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"nth-last-of-type", SLEN("nth-last-of-type"),
			&str->nth_last_of_type);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"last-child", SLEN("last-child"),
			&str->last_child);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"first-of-type", SLEN("first-of-type"),
			&str->first_of_type);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"last-of-type", SLEN("last-of-type"),
			&str->last_of_type);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"only-child", SLEN("only-child"),
			&str->only_child);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"only-of-type", SLEN("only-of-type"),
			&str->only_of_type);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"root", SLEN("root"),
			&str->root);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"empty", SLEN("empty"),
			&str->empty);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"target", SLEN("target"),
			&str->target);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"lang", SLEN("lang"),
			&str->lang);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"enabled", SLEN("enabled"),
			&str->enabled);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"disabled", SLEN("disabled"),
			&str->disabled);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"checked", SLEN("checked"),
			&str->checked);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	/* Pseudo elements */
	error = lwc_intern_string(
			"first-line", SLEN("first-line"),
			&str->first_line);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"first_letter", SLEN("first-letter"),
			&str->first_letter);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"before", SLEN("before"),
			&str->before);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"after", SLEN("after"),
			&str->after);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"width", SLEN("width"),
			&str->width);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"height", SLEN("height"),
			&str->height);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	error = lwc_intern_string(
			"prefers-color-scheme", SLEN("prefers-color-scheme"),
			&str->prefers_color_scheme);
	if (error != lwc_error_ok)
		return css_error_from_lwc_error(error);

	return CSS_OK;
}

void css_select_strings_unref(css_select_strings *str)
{
	if (str->universal != NULL)
		lwc_string_unref(str->universal);
	if (str->first_child != NULL)
		lwc_string_unref(str->first_child);
	if (str->link != NULL)
		lwc_string_unref(str->link);
	if (str->visited != NULL)
		lwc_string_unref(str->visited);
	if (str->hover != NULL)
		lwc_string_unref(str->hover);
	if (str->active != NULL)
		lwc_string_unref(str->active);
	if (str->focus != NULL)
		lwc_string_unref(str->focus);
	if (str->nth_child != NULL)
		lwc_string_unref(str->nth_child);
	if (str->nth_last_child != NULL)
		lwc_string_unref(str->nth_last_child);
	if (str->nth_of_type != NULL)
		lwc_string_unref(str->nth_of_type);
	if (str->nth_last_of_type != NULL)
		lwc_string_unref(str->nth_last_of_type);
	if (str->last_child != NULL)
		lwc_string_unref(str->last_child);
	if (str->first_of_type != NULL)
		lwc_string_unref(str->first_of_type);
	if (str->last_of_type != NULL)
		lwc_string_unref(str->last_of_type);
	if (str->only_child != NULL)
		lwc_string_unref(str->only_child);
	if (str->only_of_type != NULL)
		lwc_string_unref(str->only_of_type);
	if (str->root != NULL)
		lwc_string_unref(str->root);
	if (str->empty != NULL)
		lwc_string_unref(str->empty);
	if (str->target != NULL)
		lwc_string_unref(str->target);
	if (str->lang != NULL)
		lwc_string_unref(str->lang);
	if (str->enabled != NULL)
		lwc_string_unref(str->enabled);
	if (str->disabled != NULL)
		lwc_string_unref(str->disabled);
	if (str->checked != NULL)
		lwc_string_unref(str->checked);
	if (str->first_line != NULL)
		lwc_string_unref(str->first_line);
	if (str->first_letter != NULL)
		lwc_string_unref(str->first_letter);
	if (str->before != NULL)
		lwc_string_unref(str->before);
	if (str->after != NULL)
		lwc_string_unref(str->after);

	if (str->width != NULL)
		lwc_string_unref(str->width);
	if (str->height != NULL)
		lwc_string_unref(str->height);
	if (str->prefers_color_scheme != NULL)
		lwc_string_unref(str->prefers_color_scheme);
}
