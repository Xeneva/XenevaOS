/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#ifndef libcss_unit_h_
#define libcss_unit_h_

#ifdef __cplusplus
extern "C"
{
#endif

#include <libcss/types.h>

/**
 * Client callback for font measuring.
 *
 * \param[in]  pw     Client data.
 * \param[in]  style  Style to measure font for, or NULL.
 * \param[in]  unit   Either CSS_UNIT_EX, or CSS_UNIT_CH.
 * \return length in CSS pixels.
 */
typedef css_fixed (*css_unit_len_measure)(
		void *pw,
		const css_computed_style *style,
		const css_unit unit);

/**
 * LibCSS unit conversion context.
 *
 * The client callback is optional.  It is used for measuring "ch"
 * (glyph '0' advance) and "ex" (height of the letter 'x') units.
 * If a NULL pointer is given, LibCSS will use a fixed scaling of
 * the "em" unit.
 */
typedef struct css_unit_ctx {
	/**
	 * Viewport width in CSS pixels.
	 * Used if unit is vh, vw, vi, vb, vmin, or vmax.
	 */
	css_fixed viewport_width;
	/**
	 * Viewport height in CSS pixels.
	 * Used if unit is vh, vw, vi, vb, vmin, or vmax.
	 */
	css_fixed viewport_height;
	/**
	 * Client default font size in CSS pixels.
	 */
	css_fixed font_size_default;
	/**
	 * Client minimum font size in CSS pixels.  May be zero.
	 */
	css_fixed font_size_minimum;
	/**
	 * DPI of the device the style is selected for.
	 */
	css_fixed device_dpi;
	/**
	 * Computed style for the document root element, needed for rem units.
	 * May be NULL, in which case font_size_default is used instead, as
	 * would be the case if rem unit is used on the root element.
	 */
	const css_computed_style *root_style;
	/**
	 * Optional client private word for measure callback.
	 */
	void *pw;
	/**
	 * Optional client callback for font measuring.
	 */
	const css_unit_len_measure measure;
} css_unit_ctx;

/**
 * Convert css pixels to physical pixels.
 *
 * \param[in] css_pixels  Length in css pixels.
 * \param[in] device_dpi  Device dots per inch.
 * \return Length in device pixels.
 */
static inline css_fixed css_unit_css2device_px(
		const css_fixed css_pixels,
		const css_fixed device_dpi)
{
	return FDIV(FMUL(css_pixels, device_dpi), F_96);
}

/**
 * Convert device pixels to css pixels.
 *
 * \param[in] device_pixels  Length in physical pixels.
 * \param[in] device_dpi     Device dots per inch.
 * \return Length in css pixels.
 */
static inline css_fixed css_unit_device2css_px(
		const css_fixed device_pixels,
		const css_fixed device_dpi)
{
	return FDIV(FMUL(device_pixels, F_96), device_dpi);
}

/**
 * Convert a length to points (pt).
 *
 * \param[in]  style   Style to perform conversion for or NULL.
 * \param[in]  ctx     Length unit conversion context.
 * \param[in]  length  Length to convert.
 * \param[in]  unit    Current unit of length.
 * \return A length in points.
 */
css_fixed css_unit_font_size_len2pt(
		const css_computed_style *style,
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit);

/**
 * Convert a length to CSS pixels.
 *
 * \param[in]  style   Style to perform conversion for or NULL.
 * \param[in]  ctx     Length unit conversion context.
 * \param[in]  length  Length to convert.
 * \param[in]  unit    Current unit of length.
 * \return A length in CSS pixels.
 */
css_fixed css_unit_len2css_px(
		const css_computed_style *style,
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit);

/**
 * Convert a length to device pixels.
 *
 * \param[in]  style   Style to perform conversion for or NULL.
 * \param[in]  ctx     Length unit conversion context.
 * \param[in]  length  Length to convert.
 * \param[in]  unit    Current unit of length.
 * \return A length in device pixels.
 */
css_fixed css_unit_len2device_px(
		const css_computed_style *style,
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit);

#ifdef __cplusplus
}
#endif

#endif

