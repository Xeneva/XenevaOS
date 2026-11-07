/*
 * This file is part of LibCSS
 * Licensed under the MIT License,
 *		  http://www.opensource.org/licenses/mit-license.php
 * Copyright 2021 Vincent Sanders <vince@netsurf-browser.org>
 */

#include "select/propget.h"
#include "utils/utils.h"

#define SYMBOL_SIZE 8
typedef char symbol_t[SYMBOL_SIZE];

/**
 * numeric representation of the value using a system
 */
struct numeric {
	uint8_t *val; /* buffer containing the numeric values */
	size_t len; /* length of alen */
	size_t used; /* number of numeric values used */
	bool negative; /* if the value is negative */
};

/**
 * list counter style
 */
struct list_counter_style {
	/** style name for debug purposes */
	const char *name;
	/** function to calculate the system */
	css_error (*system)(int value, const struct list_counter_style *cstyle, struct numeric *nval);
	/** fallback style if this system fails */
	const struct list_counter_style* fallback;
	/** array of symbols which represent this style */
	const symbol_t *symbols;
	/** symbol weights for additive schemes */
	const int *weights;
	/** number of items in symbol and weight table */
	const size_t items;
	/** range of acceptable values */
	struct {
		const int start; /**< first acceptable value for this style */
		const int end; /**< last acceptable value for this style */
	} range;

	/** padding formatting */
	struct {
		const unsigned int length;
		const symbol_t value;
	} pad;
	/** negative value formating */
	struct {
		const char *pre;
		const char *post;
	} negative;
	/** prefix formatting */
	const char *prefix;
	/** suffix formatting */
	const char *suffix;
};


/**
 * Copy a null-terminated UTF-8 string to buffer at offset, if there is space
 *
 * \param[in] buf    The output buffer
 * \param[in] buflen The length of \a buf
 * \param[in] pos    Current position in \a buf
 * \param[in] str    The string to copy into \a buf
 * \return The number of bytes needed in the output buffer which may be
 *         larger than \a buflen but the buffer will not be overrun
 */
static inline size_t
copy_string(char *buf, const size_t buflen, size_t pos, const char *str)
{
	size_t sidx = 0; /* current string index */

	while (str[sidx] != '\0') {
		if (pos < buflen) {
			buf[pos] = str[sidx];
		}
		pos++;
		sidx++;
	}

	return sidx;
}

/**
 * Copy a UTF-8 symbol to buffer at offset, if there is space
 *
 * \param[in] buf    The output buffer
 * \param[in] buflen The length of \a buf
 * \param[in] pos    Current position in \a buf
 * \param[in] symbol The symbol to copy into \a buf
 * \return The number of bytes needed in the output buffer which may be
 *         larger than \a buflen but the buffer will not be overrun
 */
static inline size_t
copy_symbol(char *buf, const size_t buflen, size_t pos, const symbol_t symbol)
{
	size_t sidx = 0; /* current symbol index */

	while ((sidx < sizeof(symbol_t)) && (symbol[sidx] != '\0')) {
		if (pos < buflen) {
			buf[pos] = symbol[sidx];
		}
		pos++;
		sidx++;
	}

	return sidx;
}

/**
 * maps numeric values to output values with a symbol table
 *
 * Takes a list of numeric values and for each one outputs the
 *   compete symbol (in utf8) to an output buffer.
 *
 * \param buf The output buffer
 * \param buflen the length of \a buf
 * \param aval array of alphabet values
 * \param alen The number of values in \a alen
 * \param symtab The symbol table
 * \param symtablen The number of symbols in \a symtab
 * \return The number of bytes needed in the output buffer whichmay be
 *         larger than \a buflen but the buffer will not be overrun
 */
static size_t
nval_to_symbols(struct numeric *nval,
		const struct list_counter_style *cstyle,
		char *buf, const size_t buflen)
{
	size_t oidx = 0;
	size_t aidx; /* numeral index */
	const char *suffix = "."; /* default sufffix string */
	const char *negative_pre = "-"; /* default negative string */

	/* prefix */
	if (cstyle->prefix != NULL) {
		oidx += copy_string(buf, buflen, oidx, cstyle->prefix);
	}

	/* negative pre */
	if (nval->negative) {
		if (cstyle->negative.pre != NULL) {
			negative_pre = cstyle->negative.pre;
		}
		oidx += copy_string(buf, buflen, oidx, negative_pre);
	}

	/* add padding if required */
	if (nval->used < cstyle->pad.length) {
		size_t pidx; /* padding index */
		for (pidx = cstyle->pad.length - nval->used; pidx > 0; pidx--) {
			oidx += copy_symbol(buf, buflen, oidx,
					cstyle->pad.value);
		}
	}

	/* map symbols */
	for (aidx = 0; aidx < nval->used; aidx++) {
		oidx += copy_symbol(buf, buflen, oidx,
				cstyle->symbols[nval->val[aidx]]);
	}

	/* negative post */
	if ((nval->negative) && (cstyle->negative.post != NULL)) {
		oidx += copy_string(buf, buflen, oidx, cstyle->negative.post);
	}

	/* suffix */
	if (cstyle->suffix != NULL) {
		suffix = cstyle->suffix;
	}
	oidx += copy_string(buf, buflen, oidx, suffix);

	return oidx;
}


/**
 * generate numeric symbol values
 *
 * fills array with numeric values that represent the input value
 *
 * \param ares Buffer to recive the converted values
 * \param alen the length of \a ares buffer
 * \param value The value to convert
 * \param cstyle The counter style in use
 * \return The length a complete conversion which may be larger than \a alen
 */
static css_error
calc_numeric_system(int value,
		    const struct list_counter_style *cstyle,
		    struct numeric *nval)
{
	size_t idx = 0;
	uint8_t *first;
	uint8_t *last;

	if (value == 0) {
		if (nval->len >= 1) {
			nval->val[0] = 0;
		}
		nval->used = 1;
		return CSS_OK;
	}

	if (value < 0) {
		nval->negative = true;
		value = abs(value);
	} else {
		nval->negative = false;
	}

	/* generate alphabet values in ascending order */
	while (value > 0) {
		if (idx < nval->len) {
			nval->val[idx] = value % cstyle->items;
		}
		idx++;
		value = value / cstyle->items;
	}

	/* put the values in decending order */
	first = nval->val;
	if (idx < nval->len) {
		last = first + (idx - 1);
	} else {
		last = first + (nval->len - 1);
	}
	while (first < last) {
		*first ^= *last;
		*last ^= *first;
		*first ^= *last;
		first++;
		last--;
	}

	nval->used = idx;

	return CSS_OK;
}


/**
 * generate cyclic symbol values
 *
 * fills array with cyclic values that represent the input value
 *
 * \param ares Buffer to recive the converted values
 * \param alen the length of \a ares buffer
 * \param value The value to convert
 * \param cstyle The counter style in use
 * \return The length a complete conversion which may be larger than \a alen
 */
static css_error
calc_cyclic_system(int value,
		    const struct list_counter_style *cstyle,
		    struct numeric *nval)
{
	if (nval->len == 0) {
		return CSS_INVALID;
	}
	if (cstyle->items == 1) {
		/* there is only one symbol so select it */
		nval->val[0] = 0;
	} else {
		nval->val[0] = (value - 1) % cstyle->items;
	}
	nval->used = 1;
	nval->negative = false;

	return CSS_OK;
}


/**
 * generate addative symbol values
 *
 * fills array with numeric values that represent the input value
 *
 * \param ares Buffer to recive the converted values
 * \param alen the length of \a ares buffer
 * \param value The value to convert
 * \param wlen The number of weights
 * \return The length a complete conversion which may be larger than \a alen
 */
static css_error
calc_additive_system(int value,
		    const struct list_counter_style *cstyle,
		    struct numeric *nval)
{
	size_t widx; /* weight index */
	size_t aidx = 0; /* numerals used */
	size_t idx;
	size_t times; /* number of times a weight occours */

	/* ensure value is within acceptable range of this system */
	if ((value < cstyle->range.start) || (value > cstyle->range.end)) {
		return CSS_INVALID;
	}

	/* zero needs a specific weight */
	if (value == 0) {
		/* search weights for a zero entry */
		for (widx = 0; widx < cstyle->items; widx++) {
			if (cstyle->weights[widx] == 0) {
				if (nval->len > 0) {
					nval->val[0] = widx;
				}

				nval->used = 1;
				nval->negative = false;
				return CSS_OK;
			}
		}
		return CSS_INVALID;
	}

	/* negative values */
	if (value < 0) {
		nval->negative = true;
		value = abs(value);
	} else {
		nval->negative = false;
	}

	/* iterate over the available weights */
	for (widx = 0; widx < cstyle->items; widx++) {
		if (cstyle->weights[widx] == 0) {
			break;
		}
		times = value / cstyle->weights[widx];
		if (times > 0) {
			for (idx = 0;idx < times;idx++) {
				if (aidx < nval->len) {
					nval->val[aidx] = widx;
				}
				aidx++;
			}

			value -= times * cstyle->weights[widx];
		}
	}

	nval->used = aidx;

	return CSS_OK;
}


/**
 * generate alphabet symbol values for latin and greek labelling
 *
 * fills array with alphabet values suitable for the input value
 *
 * \param ares Buffer to recive the converted values
 * \param alen the length of \a ares buffer
 * \param value The value to convert
 * \param slen The number of symbols in the alphabet
 * \return The length a complete conversion which may be larger than \a alen
 */
static css_error
calc_alphabet_system(int value,
		     const struct list_counter_style *cstyle,
		     struct numeric *nval)
{
	size_t idx = 0;
	uint8_t *first;
	uint8_t *last;

	if (value < 1) {
		return CSS_INVALID;
	}

	/* generate alphabet values in ascending order */
	while (value > 0) {
		--value;
		if (idx < nval->len) {
			nval->val[idx] = value % cstyle->items;
		}
		idx++;
		value = value / cstyle->items;
	}

	/* put the values in decending order */
	first = nval->val;
	if (idx < nval->len) {
		last = first + (idx - 1);
	} else {
		last = first + (nval->len - 1);
	}
	while (first < last) {
		*first ^= *last;
		*last ^= *first;
		*first ^= *last;
		first++;
		last--;
	}

	nval->used = idx;
	nval->negative = false;

	return CSS_OK;
}


/**
 * Roman numeral conversion
 *
 * \return The number of numerals that are nesesary for full output
 */
static css_error
calc_roman_system(int value,
		  const struct list_counter_style *cstyle,
		  struct numeric *nval)
{
	const int S[]  = {    0,   2,   4,   2,   4,   2,   4 };
	size_t k = 0; /* index into output buffer */
	unsigned int i = 0; /* index into maps */
	int r;
	int r2 = 0;
	size_t L;

	assert(cstyle->items == 7);

	/* ensure value is within acceptable range of this system */
	if ((value < cstyle->range.start) || (value > cstyle->range.end)) {
		return CSS_INVALID;
	}

	L = cstyle->items - 1;

	while (value > 0) {
		if (cstyle->weights[i] <= value) {
			r = value / cstyle->weights[i];
			value = value - (r * cstyle->weights[i]);
			if (i < L) {
				/* lookahead */
				r2 = value / cstyle->weights[i+1];
			}
			if (i < L && r2 >= S[i+1]) {
				/* will violate repeat boundary on next pass */
				value = value - (r2 * cstyle->weights[i+1]);
				if (k < nval->len) {
					nval->val[k++] = i+1;
				}
				if (k < nval->len) {
					nval->val[k++] = i-1;
				}
			} else if (S[i] && r >= S[i]) {
				/* violated repeat boundary on this pass */
				if (k < nval->len) {
					nval->val[k++] = i;
				}
				if (k < nval->len) {
					nval->val[k++] = i-1;
				}
			} else {
				while ((r-- > 0) && (k < nval->len)) {
					nval->val[k++] = i;
				}
			}
		}
		i++;
	}

	nval->used = k;
	nval->negative = false;

	return CSS_OK;
}


/* tables for all the counter styles */

static const symbol_t decimal_symbols[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"
};
static const struct list_counter_style lcs_decimal = {
	.name = "decimal",
	.system = calc_numeric_system,
	.symbols = decimal_symbols,
	.items = (sizeof(decimal_symbols) / SYMBOL_SIZE),
};

static const symbol_t georgian_symbols[] = {
	                                        "ჵ",
	"ჰ", "ჯ", "ჴ", "ხ", "ჭ", "წ", "ძ", "ც", "ჩ",
	"შ", "ყ", "ღ", "ქ", "ფ", "ჳ", "ტ", "ს", "რ",
	"ჟ", "პ", "ო", "ჲ", "ნ", "მ", "ლ", "კ", "ი",
	"თ", "ჱ", "ზ", "ვ", "ე", "დ", "გ", "ბ", "ა",
};
static const int georgian_weights[] = {
	                                                10000,
	9000, 8000, 7000, 6000, 5000, 4000, 3000, 2000, 1000,
	900,  800,  700,  600,  500,  400,  300,  200,  100,
	90,   80,   70,   60,   50,   40,   30,   20,   10,
	9,    8,    7,    6,    5,    4,    3,    2,    1
};
static const struct list_counter_style lcs_georgian =	{
	.name="georgian",
	.system = calc_additive_system,
	.fallback = &lcs_decimal,
	.symbols = georgian_symbols,
	.weights = georgian_weights,
	.items = (sizeof(georgian_symbols) / SYMBOL_SIZE),
	.range = { .start = 1, .end = 19999 },
};


static const symbol_t upper_armenian_symbols[] = {
	"Ք", "Փ", "Ւ", "Ց", "Ր", "Տ", "Վ", "Ս", "Ռ",
	"Ջ", "Պ", "Չ", "Ո", "Շ", "Ն", "Յ", "Մ", "Ճ",
	"Ղ", "Ձ", "Հ", "Կ", "Ծ", "Խ", "Լ", "Ի", "Ժ",
	"Թ", "Ը", "Է", "Զ", "Ե", "Դ", "Գ", "Բ", "Ա"
};
static const int armenian_weights[] = {
	9000, 8000, 7000, 6000, 5000, 4000, 3000, 2000, 1000,
	900,  800,  700,  600,  500,  400,  300,  200,  100,
	90,   80,   70,   60,   50,   40,   30,   20,   10,
	9,    8,    7,    6,    5,    4,    3,    2,    1
};
static const struct list_counter_style lcs_upper_armenian = {
	.name = "upper-armenian",
	.system = calc_additive_system,
	.fallback = &lcs_decimal,
	.symbols = upper_armenian_symbols,
	.weights = armenian_weights,
	.items = (sizeof(upper_armenian_symbols) / SYMBOL_SIZE),
	.range = { .start = 1, .end = 9999 },
};


static const symbol_t lower_armenian_symbols[] = {
	"ք", "փ", "ւ", "ց", "ր", "տ", "վ", "ս", "ռ",
	"ջ", "պ", "չ", "ո", "շ", "ն", "յ", "մ", "ճ",
	"ղ", "ձ", "հ", "կ", "ծ", "խ", "լ", "ի", "ժ",
	"թ", "ը", "է", "զ", "ե", "դ", "գ", "բ", "ա"
};
static const struct list_counter_style lcs_lower_armenian = {
	.name = "lower-armenian",
	.system = calc_additive_system,
	.fallback = &lcs_decimal,
	.symbols = lower_armenian_symbols,
	.weights = armenian_weights,
	.items = (sizeof(lower_armenian_symbols) / SYMBOL_SIZE),
	.range = { .start = 1, .end = 9999 },
};


static const struct list_counter_style lcs_decimal_leading_zero = {
	.name = "decimal-leading-zero",
	.system = calc_numeric_system,
	.symbols = decimal_symbols,
	.items = (sizeof(decimal_symbols) / SYMBOL_SIZE),
	.pad = { .length = 2, .value = "0" },
};


static const symbol_t lower_greek_symbols[] = {
	"α", "β", "γ", "δ", "ε", "ζ", "η", "θ", "ι", "κ",
	"λ", "μ", "ν", "ξ", "ο", "π", "ρ", "σ", "τ", "υ",
	"φ", "χ", "ψ", "ω"
};
static const struct list_counter_style lcs_lower_greek = {
	.name = "lower-greek",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = lower_greek_symbols,
	.items = (sizeof(lower_greek_symbols) / SYMBOL_SIZE),
};


static const symbol_t upper_alpha_symbols[] = {
	"A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
	"K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
	"U", "V", "W", "X", "Y", "Z"
};
static const struct list_counter_style lcs_upper_alpha = {
	.name = "upper-alpha",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = upper_alpha_symbols,
	.items = (sizeof(upper_alpha_symbols) / SYMBOL_SIZE),
};


static const symbol_t lower_alpha_symbols[] = {
	"a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
	"k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
	"u", "v", "w", "x", "y", "z"
};
static struct list_counter_style lcs_lower_alpha = {
	.name = "lower-alpha",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = lower_alpha_symbols,
	.items = (sizeof(lower_alpha_symbols) / SYMBOL_SIZE),
};


static const int roman_weights[] = {
	1000, 500, 100,  50,  10,   5,   1
};
static const symbol_t upper_roman_symbols[] = {
	"M", "D", "C", "L", "X", "V", "I"
};
static const struct list_counter_style lcs_upper_roman = {
	.name = "upper-roman",
	.system = calc_roman_system,
	.fallback = &lcs_decimal,
	.symbols = upper_roman_symbols,
	.weights = roman_weights,
	.items = (sizeof(upper_roman_symbols) / SYMBOL_SIZE),
	.range = { .start = 1, .end = 3999 },
};


static const symbol_t lower_roman_symbols[] = {
	"m", "d", "c", "l", "x", "v", "i"
};
static const struct list_counter_style lcs_lower_roman = {
	.name = "lower-roman",
	.system = calc_roman_system,
	.fallback = &lcs_decimal,
	.symbols = lower_roman_symbols,
	.weights = roman_weights,
	.items = (sizeof(lower_roman_symbols) / SYMBOL_SIZE),
	.range = { .start = 1, .end = 3999 },
};

static const symbol_t disc_symbols[] = { "\xE2\x80\xA2"}; /* 2022 BULLET */
static const struct list_counter_style lcs_disc = {
	.name = "disc",
	.system = calc_cyclic_system,
	.symbols = disc_symbols,
	.items = (sizeof(disc_symbols) / SYMBOL_SIZE),
	.suffix = " ",
};

static const symbol_t circle_symbols[] = { "\342\227\213"}; /* 25CB WHITE CIRCLE */
static const struct list_counter_style lcs_circle = {
	.name = "circle",
	.system = calc_cyclic_system,
	.symbols = circle_symbols,
	.items = (sizeof(circle_symbols) / SYMBOL_SIZE),
	.suffix = " ",
};

static const symbol_t square_symbols[] = { "\342\226\252"}; /* 25AA BLACK SMALL SQUARE */
static const struct list_counter_style lcs_square = {
	.name = "square",
	.system = calc_cyclic_system,
	.symbols = square_symbols,
	.items = (sizeof(square_symbols) / SYMBOL_SIZE),
	.suffix = " ",
};

static const symbol_t binary_symbols[] = { "0", "1" };
static const struct list_counter_style lcs_binary = {
	.name = "binary",
	.system = calc_numeric_system,
	.symbols = binary_symbols,
	.items = (sizeof(binary_symbols) / SYMBOL_SIZE),
};

static const symbol_t octal_symbols[] = {
	"0", "1", "2", "3", "4", "5", "6", "7"
};
static const struct list_counter_style lcs_octal = {
	.name = "octal",
	.system = calc_numeric_system,
	.symbols = octal_symbols,
	.items = (sizeof(octal_symbols) / SYMBOL_SIZE),
};


static const symbol_t lower_hexadecimal_symbols[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"a", "b", "c", "d", "e", "f"
};
static const struct list_counter_style lcs_lower_hexadecimal = {
	.name = "lower-hexadecimal",
	.system = calc_numeric_system,
	.symbols = lower_hexadecimal_symbols,
	.items = (sizeof(lower_hexadecimal_symbols) / SYMBOL_SIZE),
};

static const symbol_t upper_hexadecimal_symbols[] = {
	"0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
	"a", "b", "c", "d", "e", "f"
};
static const struct list_counter_style lcs_upper_hexadecimal = {
	.name = "upper-hexadecimal",
	.system = calc_numeric_system,
	.symbols = upper_hexadecimal_symbols,
	.items = (sizeof(upper_hexadecimal_symbols) / SYMBOL_SIZE),
};

static const symbol_t arabic_indic_symbols[] = {
	"\xd9\xa0", "\xd9\xa1", "\xd9\xa2", "\xd9\xa3", "\xd9\xa4", "\xd9\xa5", "\xd9\xa6", "\xd9\xa7", "\xd9\xa8", "\xd9\xa9"
};
static const struct list_counter_style lcs_arabic_indic = {
	.name = "arabic-indic",
	.system = calc_numeric_system,
	.symbols = arabic_indic_symbols,
	.items = (sizeof(arabic_indic_symbols) / SYMBOL_SIZE),
};

static const symbol_t bengali_symbols[] = {
	"০", "১", "২", "৩", "৪", "৫", "৬", "৭", "৮", "৯"
};
static const struct list_counter_style lcs_bengali = {
	.name = "bengali",
	.system = calc_numeric_system,
	.symbols = bengali_symbols,
	.items = (sizeof(bengali_symbols) / SYMBOL_SIZE),
};

static const symbol_t cambodian_symbols[] = {
	"០", "១", "២", "៣", "៤", "៥", "៦", "៧", "៨", "៩"
};
static const struct list_counter_style lcs_cambodian = {
	.name = "cambodian",
	.system = calc_numeric_system,
	.symbols = cambodian_symbols,
	.items = (sizeof(cambodian_symbols) / SYMBOL_SIZE),
};

static const symbol_t cjk_decimal_symbols[] = {
	"〇", "一", "二", "三", "四", "五", "六", "七", "八", "九"
};
static const struct list_counter_style lcs_cjk_decimal = {
	.name = "cjk-decimal",
	.system = calc_numeric_system,
	.symbols = cjk_decimal_symbols,
	.suffix = "、",
	.items = (sizeof(cjk_decimal_symbols) / SYMBOL_SIZE),
};

static const symbol_t devanagari_symbols[] = {
	"०", "१", "२", "३", "४", "५", "६", "७", "८", "९"
};
static const struct list_counter_style lcs_devanagari = {
	.name = "devanagari",
	.system = calc_numeric_system,
	.symbols = devanagari_symbols,
	.items = (sizeof(devanagari_symbols) / SYMBOL_SIZE),
};

static const symbol_t gujarati_symbols[] = {
	"૦", "૧", "૨", "૩", "૪", "૫", "૬", "૭", "૮", "૯"
};
static const struct list_counter_style lcs_gujarati = {
	.name = "gujarati",
	.system = calc_numeric_system,
	.symbols = gujarati_symbols,
	.items = (sizeof(gujarati_symbols) / SYMBOL_SIZE),
};

static const symbol_t gurmukhi_symbols[] = {
	"੦", "੧", "੨", "੩", "੪", "੫", "੬", "੭", "੮", "੯"
};
static const struct list_counter_style lcs_gurmukhi = {
	.name = "gurmukhi",
	.system = calc_numeric_system,
	.symbols = gurmukhi_symbols,
	.items = (sizeof(gurmukhi_symbols) / SYMBOL_SIZE),
};

static const int hebrew_weights[] = {
				                                          10000,
	9000, 8000, 7000, 6000, 5000, 4000, 3000, 2000,                   1000,
	                              400,  300,  200,                    100,
	90,   80,   70,   60,   50,   40,   30,   20, 19, 18, 17, 16, 15, 10,
	9,    8,    7,    6,    5,    4,    3,    2,                      1
};
static const symbol_t hebrew_symbols[] = {
	"\xD7\x99\xD7\xB3",
	"\xD7\x98\xD7\xB3", "\xD7\x97\xD7\xB3", "\xD7\x96\xD7\xB3", "\xD7\x95\xD7\xB3", "\xD7\x94\xD7\xB3", "\xD7\x93\xD7\xB3", "\xD7\x92\xD7\xB3", "\xD7\x91\xD7\xB3", "\xD7\x90\xD7\xB3",
	"\xD7\xAA", "\xD7\xA9", "\xD7\xA8", "\xD7\xA7",
	"\xD7\xA6", "\xD7\xA4", "\xD7\xA2", "\xD7\xA1", "\xD7\xA0", "\xD7\x9E", "\xD7\x9C", /* 20 */"\xD7\x9B", "\xD7\x99\xD7\x98", "\xD7\x99\xD7\x97", "\xD7\x99\xD7\x96", "\xD7\x98\xD7\x96", "\xD7\x98\xD7\x95", "\xD7\x99",
	"\xD7\x98", "\xD7\x97", "\xD7\x96", "\xD7\x95", "\xD7\x94", "\xD7\x93", "\xD7\x92", "\xD7\x91", "\xD7\x90"
};
static const struct list_counter_style lcs_hebrew = {
	.name = "hebrew",
	.system = calc_additive_system,
	.fallback = &lcs_decimal,
	.symbols = hebrew_symbols,
	.weights = hebrew_weights,
	.items = (sizeof(hebrew_symbols) / SYMBOL_SIZE),
	.range = { .start = 1, .end = 10999 },
};

static const symbol_t kannada_symbols[] = {
	"\xE0\xB3\xA6", "\xE0\xB3\xA7", "\xE0\xB3\xA8", "\xE0\xB3\xA9", "\xE0\xB3\xAA", "\xE0\xB3\xAB", "\xE0\xB3\xAC", "\xE0\xB3\xAD", "\xE0\xB3\xAE", "\xE0\xB3\xAF"
};
static const struct list_counter_style lcs_kannada = {
	.name = "kannada",
	.system = calc_numeric_system,
	.symbols = kannada_symbols,
	.items = (sizeof(kannada_symbols) / SYMBOL_SIZE),
};

static const symbol_t lao_symbols[] = {
	"໐", "໑", "໒", "໓", "໔", "໕", "໖", "໗", "໘", "໙"
};
static const struct list_counter_style lcs_lao = {
	.name = "lao",
	.system = calc_numeric_system,
	.symbols = lao_symbols,
	.items = (sizeof(lao_symbols) / SYMBOL_SIZE),
};

static const symbol_t malayalam_symbols[] = {
	"൦", "൧", "൨", "൩", "൪", "൫", "൬", "൭", "൮", "൯"
};
static const struct list_counter_style lcs_malayalam = {
	.name = "malayalam",
	.system = calc_numeric_system,
	.symbols = malayalam_symbols,
	.items = (sizeof(malayalam_symbols) / SYMBOL_SIZE),
};

static const symbol_t mongolian_symbols[] = {
	"᠐", "᠑", "᠒", "᠓", "᠔", "᠕", "᠖", "᠗", "᠘", "᠙"
};
static const struct list_counter_style lcs_mongolian = {
	.name = "mongolian",
	.system = calc_numeric_system,
	.symbols = mongolian_symbols,
	.items = (sizeof(mongolian_symbols) / SYMBOL_SIZE),
};

static const symbol_t myanmar_symbols[] = {
	"၀", "၁", "၂", "၃", "၄", "၅", "၆", "၇", "၈", "၉"
};
static const struct list_counter_style lcs_myanmar = {
	.name = "myanmar",
	.system = calc_numeric_system,
	.symbols = myanmar_symbols,
	.items = (sizeof(myanmar_symbols) / SYMBOL_SIZE),
};

static const symbol_t oriya_symbols[] = {
	"୦", "୧", "୨", "୩", "୪", "୫", "୬", "୭", "୮", "୯"
};
static const struct list_counter_style lcs_oriya = {
	.name = "oriya",
	.system = calc_numeric_system,
	.symbols = oriya_symbols,
	.items = (sizeof(oriya_symbols) / SYMBOL_SIZE),
};

static const symbol_t persian_symbols[] = {
	"۰", "۱", "۲", "۳", "۴", "۵", "۶", "۷", "۸", "۹"
};
static const struct list_counter_style lcs_persian = {
	.name = "persian",
	.system = calc_numeric_system,
	.symbols = persian_symbols,
	.items = (sizeof(persian_symbols) / SYMBOL_SIZE),
};

static const symbol_t tamil_symbols[] = {
	"௦", "௧", "௨", "௩", "௪", "௫", "௬", "௭", "௮", "௯"
};
static const struct list_counter_style lcs_tamil = {
	.name = "tamil",
	.system = calc_numeric_system,
	.symbols = tamil_symbols,
	.items = (sizeof(tamil_symbols) / SYMBOL_SIZE),
};

static const symbol_t telugu_symbols[] = {
	"౦", "౧", "౨", "౩", "౪", "౫", "౬", "౭", "౮", "౯"
};
static const struct list_counter_style lcs_telugu = {
	.name = "telugu",
	.system = calc_numeric_system,
	.symbols = telugu_symbols,
	.items = (sizeof(telugu_symbols) / SYMBOL_SIZE),
};

static const symbol_t thai_symbols[] = {
	"๐", "๑", "๒", "๓", "๔", "๕", "๖", "๗", "๘", "๙"
};
static const struct list_counter_style lcs_thai = {
	.name = "thai",
	.system = calc_numeric_system,
	.symbols = thai_symbols,
	.items = (sizeof(thai_symbols) / SYMBOL_SIZE),
};

static const symbol_t tibetan_symbols[] = {
	"༠", "༡", "༢", "༣", "༤", "༥", "༦", "༧", "༨", "༩"
};
static const struct list_counter_style lcs_tibetan = {
	.name = "tibetan",
	.system = calc_numeric_system,
	.symbols = tibetan_symbols,
	.items = (sizeof(tibetan_symbols) / SYMBOL_SIZE),
};

static const symbol_t cjk_earthly_branch_symbols[] = {
	"子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥"
};
static struct list_counter_style lcs_cjk_earthly_branch = {
	.name = "cjk-earthly-branch",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = cjk_earthly_branch_symbols,
	.items = (sizeof(cjk_earthly_branch_symbols) / SYMBOL_SIZE),
	.suffix = "、",
};

static const symbol_t cjk_heavenly_stem_symbols[] = {
	"甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸"
};
static struct list_counter_style lcs_cjk_heavenly_stem = {
	.name = "cjk-heavenly-stem",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = cjk_heavenly_stem_symbols,
	.items = (sizeof(cjk_heavenly_stem_symbols) / SYMBOL_SIZE),
	.suffix = "、",
};

static const symbol_t hiragana_symbols[] = {
	"あ", "い", "う", "え", "お", "か", "き", "く", "け", "こ", "さ", "し", "す", "せ", "そ", "た", "ち", "つ", "て", "と", "な", "に", "ぬ", "ね", "の", "は", "ひ", "ふ", "へ", "ほ", "ま", "み", "む", "め", "も", "や", "ゆ", "よ", "ら", "り", "る", "れ", "ろ", "わ", "ゐ", "ゑ", "を", "ん"
};
static struct list_counter_style lcs_hiragana = {
	.name = "hiragana",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = hiragana_symbols,
	.items = (sizeof(hiragana_symbols) / SYMBOL_SIZE),
	.suffix = "、",
};

static const symbol_t hiragana_iroha_symbols[] = {
	"い", "ろ", "は", "に", "ほ", "へ", "と", "ち", "り", "ぬ", "る", "を", "わ", "か", "よ", "た", "れ", "そ", "つ", "ね", "な", "ら", "む", "う", "ゐ", "の", "お", "く", "や", "ま", "け", "ふ", "こ", "え", "て", "あ", "さ", "き", "ゆ", "め", "み", "し", "ゑ", "ひ", "も", "せ", "す"
};
static struct list_counter_style lcs_hiragana_iroha = {
	.name = "hiragana-iroha",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = hiragana_iroha_symbols,
	.items = (sizeof(hiragana_iroha_symbols) / SYMBOL_SIZE),
	.suffix = "、",
};

static const symbol_t katakana_symbols[] = {
	"ア", "イ", "ウ", "エ", "オ", "カ", "キ", "ク", "ケ", "コ", "サ", "シ", "ス", "セ", "ソ", "タ", "チ", "ツ", "テ", "ト", "ナ", "ニ", "ヌ", "ネ", "ノ", "ハ", "ヒ", "フ", "ヘ", "ホ", "マ", "ミ", "ム", "メ", "モ", "ヤ", "ユ", "ヨ", "ラ", "リ", "ル", "レ", "ロ", "ワ", "ヰ", "ヱ", "ヲ", "ン"
};
static struct list_counter_style lcs_katakana = {
	.name = "katakana",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = katakana_symbols,
	.items = (sizeof(katakana_symbols) / SYMBOL_SIZE),
	.suffix = "、",
};

static const symbol_t katakana_iroha_symbols[] = {
	"イ", "ロ", "ハ", "ニ", "ホ", "ヘ", "ト", "チ", "リ", "ヌ", "ル", "ヲ", "ワ", "カ", "ヨ", "タ", "レ", "ソ", "ツ", "ネ", "ナ", "ラ", "ム", "ウ", "ヰ", "ノ", "オ", "ク", "ヤ", "マ", "ケ", "フ", "コ", "エ", "テ", "ア", "サ", "キ", "ユ", "メ", "ミ", "シ", "ヱ", "ヒ", "モ", "セ", "ス"
};
static struct list_counter_style lcs_katakana_iroha = {
	.name = "katakana-iroha",
	.system = calc_alphabet_system,
	.fallback = &lcs_decimal,
	.symbols = katakana_iroha_symbols,
	.items = (sizeof(katakana_iroha_symbols) / SYMBOL_SIZE),
	.suffix = "、",
};

/**
 * weights suitable for the five complex addative styles
 */
static const int complex_counter_weights[] = {
	9000, 8000, 7000, 6000, 5000, 4000, 3000, 2000, 1000,
	900,  800,  700,  600,  500,  400,  300,  200,  100,
	90,   80,   70,   60,   50,   40,   30,   20,   10,
	9,    8,    7,    6,    5,    4,    3,    2,    1,    0
};
static const symbol_t japanese_informal_symbols[] = {
	"九千", "八千", "七千", "六千", "五千", "四千", "三千", "二千", "千",
	"九百", "八百", "七百", "六百", "五百", "四百", "三百", "二百", "百",
	"九十", "八十", "七十", "六十", "五十", "四十", "三十", "二十", "十",
	"九", "八", "七", "六", "五", "四", "三", "二", "一", "〇",
};
static const struct list_counter_style lcs_japanese_informal = {
	.name = "japanese-informal",
	.system = calc_additive_system,
	.fallback = &lcs_cjk_decimal,
	.symbols = japanese_informal_symbols,
	.weights = complex_counter_weights,
	.items = (sizeof(japanese_informal_symbols) / SYMBOL_SIZE),
	.range = { .start = -9999, .end = 9999 },
	.negative = { .pre = "マイナス" },
	.suffix = "\xE3\x80\x81",
};

static const symbol_t japanese_formal_symbols[] = {
	"九阡", "八阡", "七阡", "六阡", "伍阡", "四阡", "参阡", "弐阡", "壱阡",
	"九百", "八百", "七百", "六百", "伍百", "四百", "参百", "弐百", "壱百",
	"九拾", "八拾", "七拾", "六拾", "伍拾", "四拾", "参拾", "弐拾", "壱拾",
	"九", "八", "七", "六", "伍", "四", "参", "弐", "壱", "零",
};
static const struct list_counter_style lcs_japanese_formal = {
	.name = "japanese-formal",
	.system = calc_additive_system,
	.fallback = &lcs_cjk_decimal,
	.symbols = japanese_formal_symbols,
	.weights = complex_counter_weights,
	.items = (sizeof(japanese_formal_symbols) / SYMBOL_SIZE),
	.range = { .start = -9999, .end = 9999 },
	.negative = { .pre = "マイナス" },
	.suffix = "\xE3\x80\x81",
};

static const symbol_t korean_hangul_formal_symbols[] = {
	"구천", "팔천", "칠천", "육천", "오천", "사천", "삼천", "이천", "일천",
	"구백", "팔백", "칠백", "육백", "오백", "사백", "삼백", "이백", "일백",
	"구십", "팔십", "칠십", "육십", "오십", "사십", "삼십", "이십", "일십",
	"구", "팔", "칠", "육", "오", "사", "삼", "이", "일", "영"
};
static const struct list_counter_style lcs_korean_hangul_formal = {
	.name = "korean-hangul-formal",
	.system = calc_additive_system,
	.fallback = &lcs_cjk_decimal,
	.symbols = korean_hangul_formal_symbols,
	.weights = complex_counter_weights,
	.items = (sizeof(korean_hangul_formal_symbols) / SYMBOL_SIZE),
	.range = { .start = -9999, .end = 9999 },
	.negative = { .pre = "\xEB\xA7\x88\xEC\x9D\xB4\xEB\x84\x88\xEC\x8A\xA4 " },
	.suffix = ", ",
};

static const symbol_t korean_hanja_informal_symbols[] = {
	"九千", "八千", "七千", "六千", "五千", "四千", "三千", "二千", "千",
	"九百", "八百", "七百", "六百", "五百", "四百", "三百", "二百", "百",
	"九十", "八十", "七十", "六十", "五十", "四十", "三十", "二十", "十",
	"九", "八", "七", "六", "五", "四", "三", "二", "一", "零"
};
static const struct list_counter_style lcs_korean_hanja_informal = {
	.name = "korean-hanja-informal",
	.system = calc_additive_system,
	.fallback = &lcs_cjk_decimal,
	.symbols = korean_hanja_informal_symbols,
	.weights = complex_counter_weights,
	.items = (sizeof(korean_hanja_informal_symbols) / SYMBOL_SIZE),
	.range = { .start = -9999, .end = 9999 },
	.negative = { .pre = "\xEB\xA7\x88\xEC\x9D\xB4\xEB\x84\x88\xEC\x8A\xA4 " },
	.suffix = ", ",
};

static const symbol_t korean_hanja_formal_symbols[] = {
	"九仟", "八仟", "七仟", "六仟", "五仟", "四仟", "參仟", "貳仟", "壹仟",
	"九百", "八百", "七百", "六百", "五百", "四百", "參百", "貳百", "壹百",
	"九拾", "八拾", "七拾", "六拾", "五拾", "四拾", "參拾", "貳拾", "壹拾",
	"九", "八", "七", "六", "五", "四", "參", "貳", "壹", "零"
};
static const struct list_counter_style lcs_korean_hanja_formal = {
	.name = "korean-hanja-formal",
	.system = calc_additive_system,
	.fallback = &lcs_cjk_decimal,
	.symbols = korean_hanja_formal_symbols,
	.weights = complex_counter_weights,
	.items = (sizeof(korean_hanja_formal_symbols) / SYMBOL_SIZE),
	.range = { .start = -9999, .end = 9999 },
	.negative = { .pre = "\xEB\xA7\x88\xEC\x9D\xB4\xEB\x84\x88\xEC\x8A\xA4 " },
	.suffix = ", ",
};



static const struct list_counter_style *
counter_style_from_computed_style(const css_computed_style *style)
{
	switch (get_list_style_type(style)) {
	case CSS_LIST_STYLE_TYPE_DISC:
		return &lcs_disc;
	case CSS_LIST_STYLE_TYPE_CIRCLE:
		return &lcs_circle;
	case CSS_LIST_STYLE_TYPE_SQUARE:
		return &lcs_square;
	case CSS_LIST_STYLE_TYPE_DECIMAL:
		return &lcs_decimal;
	case CSS_LIST_STYLE_TYPE_DECIMAL_LEADING_ZERO:
		return &lcs_decimal_leading_zero;
	case CSS_LIST_STYLE_TYPE_LOWER_ROMAN:
		return &lcs_lower_roman;
	case CSS_LIST_STYLE_TYPE_UPPER_ROMAN:
		return &lcs_upper_roman;
	case CSS_LIST_STYLE_TYPE_LOWER_GREEK:
		return &lcs_lower_greek;
	case CSS_LIST_STYLE_TYPE_LOWER_ALPHA:
	case CSS_LIST_STYLE_TYPE_LOWER_LATIN:
		return &lcs_lower_alpha;
	case CSS_LIST_STYLE_TYPE_UPPER_ALPHA:
	case CSS_LIST_STYLE_TYPE_UPPER_LATIN:
		return &lcs_upper_alpha;
	case CSS_LIST_STYLE_TYPE_UPPER_ARMENIAN:
	case CSS_LIST_STYLE_TYPE_ARMENIAN:
		return &lcs_upper_armenian;
	case CSS_LIST_STYLE_TYPE_GEORGIAN:
		return &lcs_georgian;
	case CSS_LIST_STYLE_TYPE_NONE:
		return NULL;
	case CSS_LIST_STYLE_TYPE_BINARY:
		return &lcs_binary;
	case CSS_LIST_STYLE_TYPE_OCTAL:
		return &lcs_octal;
	case CSS_LIST_STYLE_TYPE_LOWER_HEXADECIMAL:
		return &lcs_lower_hexadecimal;
	case CSS_LIST_STYLE_TYPE_UPPER_HEXADECIMAL:
		return &lcs_upper_hexadecimal;
	case CSS_LIST_STYLE_TYPE_ARABIC_INDIC:
		return &lcs_arabic_indic;
	case CSS_LIST_STYLE_TYPE_LOWER_ARMENIAN:
		return &lcs_lower_armenian;
	case CSS_LIST_STYLE_TYPE_BENGALI:
		return &lcs_bengali;
	case CSS_LIST_STYLE_TYPE_CAMBODIAN:
	case CSS_LIST_STYLE_TYPE_KHMER:
		return &lcs_cambodian;
	case CSS_LIST_STYLE_TYPE_CJK_DECIMAL:
		return &lcs_cjk_decimal;
	case CSS_LIST_STYLE_TYPE_DEVANAGARI:
		return &lcs_devanagari;
	case CSS_LIST_STYLE_TYPE_GUJARATI:
		return &lcs_gujarati;
	case CSS_LIST_STYLE_TYPE_GURMUKHI:
		return &lcs_gurmukhi;
	case CSS_LIST_STYLE_TYPE_HEBREW:
		return &lcs_hebrew;
	case CSS_LIST_STYLE_TYPE_KANNADA:
		return &lcs_kannada;
	case CSS_LIST_STYLE_TYPE_LAO:
		return &lcs_lao;
	case CSS_LIST_STYLE_TYPE_MALAYALAM:
		return &lcs_malayalam;
	case CSS_LIST_STYLE_TYPE_MONGOLIAN:
		return &lcs_mongolian;
	case CSS_LIST_STYLE_TYPE_MYANMAR:
		return &lcs_myanmar;
	case CSS_LIST_STYLE_TYPE_ORIYA:
		return &lcs_oriya;
	case CSS_LIST_STYLE_TYPE_PERSIAN:
		return &lcs_persian;
	case CSS_LIST_STYLE_TYPE_TAMIL:
		return &lcs_tamil;
	case CSS_LIST_STYLE_TYPE_TELUGU:
		return &lcs_telugu;
	case CSS_LIST_STYLE_TYPE_THAI:
		return &lcs_thai;
	case CSS_LIST_STYLE_TYPE_TIBETAN:
		return &lcs_tibetan;
	case CSS_LIST_STYLE_TYPE_CJK_EARTHLY_BRANCH:
		return &lcs_cjk_earthly_branch;
	case CSS_LIST_STYLE_TYPE_CJK_HEAVENLY_STEM:
		return &lcs_cjk_heavenly_stem;
	case CSS_LIST_STYLE_TYPE_HIAGANA:
		return &lcs_hiragana;
	case CSS_LIST_STYLE_TYPE_HIAGANA_IROHA:
		return &lcs_hiragana_iroha;
	case CSS_LIST_STYLE_TYPE_KATAKANA:
		return &lcs_katakana;
	case CSS_LIST_STYLE_TYPE_KATAKANA_IROHA:
		return &lcs_katakana_iroha;
	case CSS_LIST_STYLE_TYPE_JAPANESE_INFORMAL:
		return &lcs_japanese_informal;
	case CSS_LIST_STYLE_TYPE_JAPANESE_FORMAL:
		return &lcs_japanese_formal;
	case CSS_LIST_STYLE_TYPE_KOREAN_HANGUL_FORMAL:
		return &lcs_korean_hangul_formal;
	case CSS_LIST_STYLE_TYPE_KOREAN_HANJA_INFORMAL:
		return &lcs_korean_hanja_informal;
	case CSS_LIST_STYLE_TYPE_KOREAN_HANJA_FORMAL:
		return &lcs_korean_hanja_formal;
	}
	return NULL;
}


/* exported interface defined in select.h */
css_error css_computed_format_list_style(
		const css_computed_style *style,
		int value,
		char *buffer,
		size_t buffer_length,
		size_t *format_length)
{
	css_error res = CSS_INVALID;
	const struct list_counter_style *cstyle;
	uint8_t aval[20];
	struct numeric nval = {
		.val = aval,
		.len = sizeof(aval),
		.used = 0,
		.negative = false
	};


	cstyle = counter_style_from_computed_style(style);
	while (cstyle != NULL) {
		res = cstyle->system(value, cstyle, &nval);

		if ((res == CSS_OK) &&
		    (nval.used < nval.len)) {
			/* system sucessfully generated numeric values */
			*format_length = nval_to_symbols(&nval,
							 cstyle,
							 buffer,
							 buffer_length);
			break;
		}

		res = CSS_INVALID;
		cstyle = cstyle->fallback;
	}

	return res;
}
