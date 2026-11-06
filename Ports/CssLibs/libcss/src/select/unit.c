/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 *
 * Copyright 2021 Michael Drake <tlsa@netsurf-browser.org>
 */

#include <libcss/stylesheet.h>

#include "utils/utils.h"

#include "propget.h"
#include "unit.h"

/**
 * Map viewport-relative length units to either vh or vw.
 *
 * Non-viewport-relative units are unchanged.
 *
 * \param[in] style            Reference style.
 * \param[in] viewport_height  Viewport height in px.
 * \param[in] viewport_width   Viewport width in px.
 * \param[in] unit             Unit to map.
 * \return the mapped unit.
 */
static inline css_unit css_unit__map_viewport_units(
		const css_computed_style *style,
		const css_fixed viewport_height,
		const css_fixed viewport_width,
		const css_unit unit)
{
	switch (unit) {
	case CSS_UNIT_VI:
		return (style != NULL && get_writing_mode(style) !=
				CSS_WRITING_MODE_HORIZONTAL_TB) ?
				CSS_UNIT_VH : CSS_UNIT_VW;

	case CSS_UNIT_VB:
		return (style != NULL && get_writing_mode(style) !=
				CSS_WRITING_MODE_HORIZONTAL_TB) ?
				CSS_UNIT_VW : CSS_UNIT_VH;

	case CSS_UNIT_VMIN:
		return (viewport_height < viewport_width) ?
				CSS_UNIT_VH : CSS_UNIT_VW;

	case CSS_UNIT_VMAX:
		return (viewport_height > viewport_width) ?
				CSS_UNIT_VH : CSS_UNIT_VW;

	default:
		return unit;
	}
}

/**
 * Convert an absolute length to points (pt).
 *
 * \param[in] style            Style to get font-size from, or NULL.
 * \param[in] viewport_height  Client viewport height.
 * \param[in] viewport_width   Client viewport width.
 * \param[in] length           Length to convert.
 * \param[in] unit             Current unit of length.
 * \return length in points (pt).
 */
static inline css_fixed css_unit__absolute_len2pt(
		const css_computed_style *style,
		const css_fixed viewport_height,
		const css_fixed viewport_width,
		const css_fixed length,
		const css_unit unit)
{
	/* Length must not be relative */
	assert(unit != CSS_UNIT_EM &&
	       unit != CSS_UNIT_EX &&
	       unit != CSS_UNIT_CH &&
	       unit != CSS_UNIT_REM);

	switch (css_unit__map_viewport_units(style,
			viewport_height,
			viewport_width,
			unit)) {
	case CSS_UNIT_PX:
		return FDIV(FMUL(length, F_72), F_96);

	case CSS_UNIT_IN:
		return FMUL(length, F_72);

	case CSS_UNIT_CM:
		return FMUL(length, FDIV(F_72, FLTTOFIX(2.54)));

	case CSS_UNIT_MM:
		return FMUL(length, FDIV(F_72, FLTTOFIX(25.4)));

	case CSS_UNIT_Q:
		return FMUL(length, FDIV(F_72, FLTTOFIX(101.6)));

	case CSS_UNIT_PT:
		return length;

	case CSS_UNIT_PC:
		return FMUL(length, INTTOFIX(12));

	case CSS_UNIT_VH:
		return FDIV(FMUL(FDIV(FMUL(length, viewport_height), F_100),
				F_72), F_96);

	case CSS_UNIT_VW:
		return FDIV(FMUL(FDIV(FMUL(length, viewport_width), F_100),
				F_72), F_96);

	default:
		return 0;
	}
}

/* Exported function, documented in libcss/unit.h. */
css_fixed css_unit_font_size_len2pt(
		const css_computed_style *style,
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit)
{
	return css_unit__absolute_len2pt(
			style,
			ctx->viewport_height,
			ctx->viewport_width,
			length,
			unit);
}

/**
 * Get font size from a style in CSS pixels.
 *
 * The style should have font size in absolute units.
 *
 * \param[in] style              Style to get font-size from, or NULL.
 * \param[in] font_size_default  Client font size for NULL style.
 * \param[in] font_size_minimum  Client minimum font size clamp.
 * \param[in] viewport_height    Client viewport height.
 * \param[in] viewport_width     Client viewport width.
 * \return font-size in CSS pixels.
 */
static inline css_fixed css_unit__font_size_px(
		const css_computed_style *style,
		const css_fixed font_size_default,
		const css_fixed font_size_minimum,
		const css_fixed viewport_height,
		const css_fixed viewport_width)
{
	css_fixed font_length = 0;
	css_unit font_unit = CSS_UNIT_PT;

	if (style == NULL) {
		return font_size_default;
	}

	get_font_size(style, &font_length, &font_unit);

	if (font_unit != CSS_UNIT_PX) {
		font_length = css_unit__absolute_len2pt(style,
				viewport_height,
				viewport_width,
				font_length,
				font_unit);

		/* Convert from pt to CSS pixels.*/
		font_length = FDIV(FMUL(font_length, F_96), F_72);
	}

	/* Clamp to configured minimum */
	if (font_length < font_size_minimum) {
		font_length = font_size_minimum;
	}

	return font_length;
}

/**
 * Get the number of CSS pixels for a given unit.
 *
 * \param[in] measure            Client callback for font measuring.
 * \param[in] ref_style          Reference style.  (Element or parent, or NULL).
 * \param[in] root_style         Root element style or NULL.
 * \param[in] font_size_default  Client default font size in CSS pixels.
 * \param[in] font_size_minimum  Client minimum font size in CSS pixels.
 * \param[in] viewport_height    Viewport height in CSS pixels.
 * \param[in] viewport_width     Viewport width in CSS pixels.
 * \param[in] unit               The unit to convert from.
 * \param[in] pw                 Client private word for measure callback.
 * \return Number of CSS pixels equivalent to the given unit.
 */
static inline css_fixed css_unit__px_per_unit(
		const css_unit_len_measure measure,
		const css_computed_style *ref_style,
		const css_computed_style *root_style,
		const css_fixed font_size_default,
		const css_fixed font_size_minimum,
		const css_fixed viewport_height,
		const css_fixed viewport_width,
		const css_unit unit,
		void *pw)
{
	switch (css_unit__map_viewport_units(
			ref_style,
			viewport_height,
			viewport_width,
			unit)) {
	case CSS_UNIT_EM:
		return css_unit__font_size_px(
				ref_style,
				font_size_default,
				font_size_minimum,
				viewport_height,
				viewport_width);

	case CSS_UNIT_EX:
		if (measure != NULL) {
			return measure(pw, ref_style, CSS_UNIT_EX);
		}
		return FMUL(css_unit__font_size_px(
				ref_style,
				font_size_default,
				font_size_minimum,
				viewport_height,
				viewport_width), FLTTOFIX(0.6));

	case CSS_UNIT_CH:
		if (measure != NULL) {
			return measure(pw, ref_style, CSS_UNIT_CH);
		}
		return FMUL(css_unit__font_size_px(
				ref_style,
				font_size_default,
				font_size_minimum,
				viewport_height,
				viewport_width), FLTTOFIX(0.4));

	case CSS_UNIT_PX:
		return F_1;

	case CSS_UNIT_IN:
		return F_96;

	case CSS_UNIT_CM:
		return FDIV(F_96, FLTTOFIX(2.54));

	case CSS_UNIT_MM:
		return FDIV(F_96, FLTTOFIX(25.4));

	case CSS_UNIT_Q:
		return FDIV(F_96, FLTTOFIX(101.6));

	case CSS_UNIT_PT:
		return FDIV(F_96, F_72);

	case CSS_UNIT_PC:
		return FDIV(F_96, INTTOFIX(6));

	case CSS_UNIT_REM:
		return css_unit__font_size_px(
				root_style,
				font_size_default,
				font_size_minimum,
				viewport_height,
				viewport_width);

	case CSS_UNIT_VH:
		return FDIV(viewport_width, F_100);

	case CSS_UNIT_VW:
		return FDIV(viewport_height, F_100);

	default:
		return 0;
	}
}

/* Exported function, documented in unit.h. */
css_fixed css_unit_len2px_mq(
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit)
{
	/* In the media query context there is no reference or root element
	 * style, so these are hard-coded to NULL. */
	css_fixed px_per_unit = css_unit__px_per_unit(
			ctx->measure,
			NULL,
			NULL,
			ctx->font_size_default,
			ctx->font_size_minimum,
			ctx->viewport_height,
			ctx->viewport_width,
			unit,
			ctx->pw);

	/* Ensure we round px_per_unit to the nearest whole number of pixels:
	 * the use of FIXTOINT() below will truncate. */
	px_per_unit += F_0_5;

	/* Calculate total number of pixels */
	return FMUL(length, TRUNCATEFIX(px_per_unit));
}

/* Exported function, documented in libcss/unit.h. */
css_fixed css_unit_len2css_px(
		const css_computed_style *style,
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit)
{
	css_fixed px_per_unit = css_unit__px_per_unit(
			ctx->measure,
			style,
			ctx->root_style,
			ctx->font_size_default,
			ctx->font_size_minimum,
			ctx->viewport_height,
			ctx->viewport_width,
			unit,
			ctx->pw);

	/* Ensure we round px_per_unit to the nearest whole number of pixels:
	 * the use of FIXTOINT() below will truncate. */
	px_per_unit += F_0_5;

	/* Calculate total number of pixels */
	return FMUL(length, TRUNCATEFIX(px_per_unit));
}

/* Exported function, documented in libcss/unit.h. */
css_fixed css_unit_len2device_px(
		const css_computed_style *style,
		const css_unit_ctx *ctx,
		const css_fixed length,
		const css_unit unit)
{
	css_fixed px_per_unit = css_unit__px_per_unit(
			ctx->measure,
			style,
			ctx->root_style,
			ctx->font_size_default,
			ctx->font_size_minimum,
			ctx->viewport_height,
			ctx->viewport_width,
			unit,
			ctx->pw);

	px_per_unit = css_unit_css2device_px(px_per_unit, ctx->device_dpi);

	/* Ensure we round px_per_unit to the nearest whole number of pixels:
	 * the use of FIXTOINT() below will truncate. */
	px_per_unit += F_0_5;

	/* Calculate total number of pixels */
	return FMUL(length, TRUNCATEFIX(px_per_unit));
}

/**
 * Get font size from a computed style.
 *
 * The computed style will have font-size with an absolute unit.
 * If no computed style is given, the client default font-size will be returned.
 *
 * \param[in] style              Reference style.  (Element or parent, or NULL).
 * \param[in] font_size_default  Client default font size in CSS pixels.
 * \return The font size in absolute units.
 */
static inline css_hint_length css_unit__get_font_size(
		const css_computed_style *style,
		css_fixed font_size_default)
{
	css_hint_length size = {
		.value = font_size_default,
		.unit = CSS_UNIT_PX,
	};

	if (style != NULL) {
		enum css_font_size_e status = get_font_size(style,
				&size.value,
				&size.unit);

		UNUSED(status);

		/* The font size must be absolute. */
		assert(status == CSS_FONT_SIZE_DIMENSION);
		assert(size.unit != CSS_UNIT_EM);
		assert(size.unit != CSS_UNIT_EX);
		assert(size.unit != CSS_UNIT_PCT);
	}

	return size;
}

/* Exported function, documented in unit.h. */
css_error css_unit_compute_absolute_font_size(
		const css_hint_length *ref_length,
		const css_computed_style *root_style,
		css_fixed font_size_default,
		css_hint *size)
{
	css_hint_length ref_len = {
		.value = font_size_default,
		.unit = CSS_UNIT_PX,
	};

	if (ref_length != NULL) {
		/* Must be absolute. */
		assert(ref_length->unit != CSS_UNIT_EM);
		assert(ref_length->unit != CSS_UNIT_EX);
		assert(ref_length->unit != CSS_UNIT_PCT);

		ref_len = *ref_length;
	}

	assert(size->status != CSS_FONT_SIZE_INHERIT);

	switch (size->status) {
	case CSS_FONT_SIZE_XX_SMALL: /* Fall-through. */
	case CSS_FONT_SIZE_X_SMALL:  /* Fall-through. */
	case CSS_FONT_SIZE_SMALL:    /* Fall-through. */
	case CSS_FONT_SIZE_MEDIUM:   /* Fall-through. */
	case CSS_FONT_SIZE_LARGE:    /* Fall-through. */
	case CSS_FONT_SIZE_X_LARGE:  /* Fall-through. */
	case CSS_FONT_SIZE_XX_LARGE:
	{
		static const css_fixed factors[CSS_FONT_SIZE_XX_LARGE] = {
			[CSS_FONT_SIZE_XX_SMALL - 1] = FLTTOFIX(0.5625),
			[CSS_FONT_SIZE_X_SMALL  - 1] = FLTTOFIX(0.6250),
			[CSS_FONT_SIZE_SMALL    - 1] = FLTTOFIX(0.8125),
			[CSS_FONT_SIZE_MEDIUM   - 1] = FLTTOFIX(1.0000),
			[CSS_FONT_SIZE_LARGE    - 1] = FLTTOFIX(1.1250),
			[CSS_FONT_SIZE_X_LARGE  - 1] = FLTTOFIX(1.5000),
			[CSS_FONT_SIZE_XX_LARGE - 1] = FLTTOFIX(2.0000),
		};
		assert(CSS_FONT_SIZE_INHERIT  == 0);
		assert(CSS_FONT_SIZE_XX_SMALL == 1);

		size->data.length.value = FMUL(factors[size->status - 1],
				font_size_default);
		size->data.length.unit = CSS_UNIT_PX;
		size->status = CSS_FONT_SIZE_DIMENSION;
		break;
	}
	case CSS_FONT_SIZE_LARGER:
		size->data.length.value = FMUL(ref_len.value, FLTTOFIX(1.2));
		size->data.length.unit = ref_len.unit;
		size->status = CSS_FONT_SIZE_DIMENSION;
		break;

	case CSS_FONT_SIZE_SMALLER:
		size->data.length.value = FDIV(ref_len.value, FLTTOFIX(1.2));
		size->data.length.unit = ref_len.unit;
		size->status = CSS_FONT_SIZE_DIMENSION;
		break;

	case CSS_FONT_SIZE_DIMENSION:
		/* Convert any relative units to absolute. */
		switch (size->data.length.unit) {
		case CSS_UNIT_PCT:
			size->data.length.value = FDIV(FMUL(
					size->data.length.value,
					ref_len.value), INTTOFIX(100));
			size->data.length.unit = ref_len.unit;
			break;

		case CSS_UNIT_EM: /* Fall-through */
		case CSS_UNIT_EX: /* Fall-through */
		case CSS_UNIT_CH:
			/* Parent relative units. */
			size->data.length.value = FMUL(
					size->data.length.value, ref_len.value);

			switch (size->data.length.unit) {
			case CSS_UNIT_EX:
				size->data.length.value = FMUL(
						size->data.length.value,
						FLTTOFIX(0.6));
				break;

			case CSS_UNIT_CH:
				size->data.length.value = FMUL(
						size->data.length.value,
						FLTTOFIX(0.4));
				break;

			default:
				break;
			}
			size->data.length.unit = ref_len.unit;
			break;

		case CSS_UNIT_REM:
			/* Root element relative units. */
			ref_len = css_unit__get_font_size(root_style,
					font_size_default);

			size->data.length.unit = ref_len.unit;
			size->data.length.value = FMUL(
					size->data.length.value, ref_len.value);
			break;

		default:
			break;
		}
	default:
		break;
	}

	return CSS_OK;
}
