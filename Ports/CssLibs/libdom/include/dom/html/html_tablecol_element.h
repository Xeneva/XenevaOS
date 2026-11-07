/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1v_alignr99@gmail.com>
 */

#ifndef dom_html_table_col_element_h_
#define dom_html_table_col_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_table_col_element dom_html_table_col_element;

dom_exception dom_html_table_col_element_get_align(
	dom_html_table_col_element *table_col, dom_string **align);

dom_exception dom_html_table_col_element_set_align(
	dom_html_table_col_element *table_col, dom_string *align);

dom_exception dom_html_table_col_element_get_ch(
	dom_html_table_col_element *table_col, dom_string **ch);

dom_exception dom_html_table_col_element_set_ch(
	dom_html_table_col_element *table_col, dom_string *ch);

dom_exception dom_html_table_col_element_get_ch_off(
	dom_html_table_col_element *table_col, dom_string **ch_off);

dom_exception dom_html_table_col_element_set_ch_off(
	dom_html_table_col_element *table_col, dom_string *ch_off);

dom_exception dom_html_table_col_element_get_v_align(
	dom_html_table_col_element *table_col, dom_string **v_align);

dom_exception dom_html_table_col_element_set_v_align(
	dom_html_table_col_element *table_col, dom_string *v_align);

dom_exception dom_html_table_col_element_get_width(
	dom_html_table_col_element *table_col, dom_string **width);

dom_exception dom_html_table_col_element_set_width(
	dom_html_table_col_element *table_col, dom_string *width);

dom_exception dom_html_table_col_element_get_span(
	dom_html_table_col_element *table_col, int32_t *span);

dom_exception dom_html_table_col_element_set_span(
	dom_html_table_col_element *table_col, uint32_t span);

#endif

