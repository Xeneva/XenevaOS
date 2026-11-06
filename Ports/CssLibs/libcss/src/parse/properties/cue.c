/*
 * This file is part of LibCSS.
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <assert.h>
#include <string.h>

#include "bytecode/bytecode.h"
#include "bytecode/opcodes.h"
#include "parse/properties/properties.h"
#include "parse/properties/utils.h"

/**
 * Parse cue shorthand
 *
 * \param c	  Parsing context
 * \param vector  Vector of tokens to process
 * \param ctx	  Pointer to vector iteration context
 * \param result  Pointer to location to receive resulting style
 * \return CSS_OK on success,
 *	   CSS_NOMEM on memory exhaustion,
 *	   CSS_INVALID if the input is not valid
 *
 * Post condition: \a *ctx is updated with the next token to process
 *		   If the input is invalid, then \a *ctx remains unchanged.
 */
css_error css__parse_cue(css_language *c,
		const parserutils_vector *vector, int32_t *ctx,
		css_style *result)
{
	int32_t orig_ctx = *ctx;
	css_error error;
	const css_token *first_token;
	const css_token *token;

	/* one or two tokens follow:
	 *  if one emit for both BEFORE and AFTER
	 *  if two first is before second is after
	 *  tokens are either IDENT:none or URI
	 */

	first_token = parserutils_vector_peek(vector, *ctx);

	error = css__parse_cue_before(c, vector, ctx, result);
	if (error == CSS_OK) {
		/* first token parsed */

		consumeWhitespace(vector, ctx);

		token = parserutils_vector_peek(vector, *ctx);
		if (token == NULL)  {
			/* no second token, re-parse the first */
			*ctx = orig_ctx;
			error = css__parse_cue_after(c, vector, ctx, result);
		} else {
			/* second token - might be useful */
			enum flag_value flag_value;

			flag_value = get_css_flag_value(c, token);

			if (flag_value != FLAG_VALUE__NONE) {
				/* another generic property reset value
				 * which is bogus */
				error = CSS_INVALID;
			} else {
				error = css__parse_cue_after(c, vector, ctx, result);
				if (error == CSS_OK) {
					/* second token parsed */
					flag_value = get_css_flag_value(c, first_token);

					if (flag_value != FLAG_VALUE__NONE) {
						/* valid second token after
						 * generic property reset value */
						error = CSS_INVALID;
					}
				} else {
					/* second token appears to be junk re-try with first */
					*ctx = orig_ctx;
					error = css__parse_cue_after(c, vector, ctx, result);
				}
			}
		}
	}


	if (error != CSS_OK)
		*ctx = orig_ctx;

	return error;
}

