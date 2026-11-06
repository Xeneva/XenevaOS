/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Copyright 2021 Michael Drake <tlsa@netsurf-browser.org>
 */

#ifndef css_select_unit_h_
#define css_select_unit_h_

#include <libcss/unit.h>

/**
 * Convert a length to CSS pixels for a media query context.
 *
 * \param[in]  ctx     Document unit conversion context.
 * \param[in]  length  Length to convert.
 * \param[in]  unit    Current unit of length.
 * \return A length in CSS pixels.
 */
css_fixed css_unit_len2px_mq(
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit);

/**
 * Convert relative font size units to absolute units.
 *
 * \param[in] ref_length         Reference font-size length or NULL.
 * \param[in] root_style         Root element style or NULL.
 * \param[in] font_size_default  Client default font size in CSS pixels.
 * \param[in,out] size           The length to convert.
 * \return CSS_OK on success, or appropriate error otherwise.
 */
css_error css_unit_compute_absolute_font_size(
		const css_hint_length *ref_length,
		const css_computed_style *root_style,
		css_fixed font_size_default,
		css_hint *size);

#endif
