/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1bg_colorr99@gmail.com>
 */

#ifndef dom_html_table_cell_element_h_
#define dom_html_table_cell_element_h_

#include <stdbool.h>

#include <dom/inttypes.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_table_cell_element dom_html_table_cell_element;

dom_exception dom_html_table_cell_element_get_cell_index(
	dom_html_table_cell_element *table_cell, dom_long *cell_index);

dom_exception dom_html_table_cell_element_get_abbr(
	dom_html_table_cell_element *table_cell, dom_string **abbr);

dom_exception dom_html_table_cell_element_set_abbr(
	dom_html_table_cell_element *table_cell, dom_string *abbr);

dom_exception dom_html_table_cell_element_get_align(
	dom_html_table_cell_element *table_cell, dom_string **align);

dom_exception dom_html_table_cell_element_set_align(
	dom_html_table_cell_element *table_cell, dom_string *align);

dom_exception dom_html_table_cell_element_get_axis(
	dom_html_table_cell_element *table_cell, dom_string **axis);

dom_exception dom_html_table_cell_element_set_axis(
	dom_html_table_cell_element *table_cell, dom_string *axis);

dom_exception dom_html_table_cell_element_get_bg_color(
	dom_html_table_cell_element *table_cell, dom_string **bg_color);

dom_exception dom_html_table_cell_element_set_bg_color(
	dom_html_table_cell_element *table_cell, dom_string *bg_color);

dom_exception dom_html_table_cell_element_get_ch(
	dom_html_table_cell_element *table_cell, dom_string **ch);

dom_exception dom_html_table_cell_element_set_ch(
	dom_html_table_cell_element *table_cell, dom_string *ch);

dom_exception dom_html_table_cell_element_get_ch_off(
	dom_html_table_cell_element *table_cell, dom_string **ch_off);

dom_exception dom_html_table_cell_element_set_ch_off(
	dom_html_table_cell_element *table_cell, dom_string *ch_off);

dom_exception dom_html_table_cell_element_get_headers(
	dom_html_table_cell_element *table_cell, dom_string **headers);

dom_exception dom_html_table_cell_element_set_headers(
	dom_html_table_cell_element *table_cell, dom_string *headers);

dom_exception dom_html_table_cell_element_get_height(
	dom_html_table_cell_element *table_cell, dom_string **height);

dom_exception dom_html_table_cell_element_set_height(
	dom_html_table_cell_element *table_cell, dom_string *height);

dom_exception dom_html_table_cell_element_get_scope(
	dom_html_table_cell_element *table_cell, dom_string **scope);

dom_exception dom_html_table_cell_element_set_scope(
	dom_html_table_cell_element *table_cell, dom_string *scope);

dom_exception dom_html_table_cell_element_get_v_align(
	dom_html_table_cell_element *table_cell, dom_string **v_align);

dom_exception dom_html_table_cell_element_set_v_align(
	dom_html_table_cell_element *table_cell, dom_string *v_align);

dom_exception dom_html_table_cell_element_get_width(
	dom_html_table_cell_element *table_cell, dom_string **width);

dom_exception dom_html_table_cell_element_set_width(
	dom_html_table_cell_element *table_cell, dom_string *width);

dom_exception dom_html_table_cell_element_get_col_span(
	dom_html_table_cell_element *table_cell, dom_ulong *col_span);

dom_exception dom_html_table_cell_element_set_col_span(
	dom_html_table_cell_element *table_cell, dom_ulong col_span);

dom_exception dom_html_table_cell_element_get_row_span(
	dom_html_table_cell_element *table_cell, dom_ulong *row_span);

dom_exception dom_html_table_cell_element_set_row_span(
	dom_html_table_cell_element *table_cell, dom_ulong row_span);

dom_exception dom_html_table_cell_element_get_no_wrap(
		        dom_html_table_cell_element *ele, bool *no_wrap);

dom_exception dom_html_table_cell_element_set_no_wrap(
		        dom_html_table_cell_element *ele, bool no_wrap);

#endif

