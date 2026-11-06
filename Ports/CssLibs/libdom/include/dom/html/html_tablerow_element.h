/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_table_row_element_h_
#define dom_html_table_row_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include<dom/html/html_element.h>
#include<dom/html/html_collection.h>

typedef struct dom_html_table_row_element dom_html_table_row_element;

dom_exception dom_html_table_row_element_get_align(
		dom_html_table_row_element *table, dom_string **align);

dom_exception dom_html_table_row_element_set_align(
		dom_html_table_row_element *table, dom_string *align);

dom_exception dom_html_table_row_element_get_bg_color(
		dom_html_table_row_element *table, dom_string **bg_color);

dom_exception dom_html_table_row_element_set_bg_color(
		dom_html_table_row_element *table, dom_string *bg_color);

dom_exception dom_html_table_row_element_get_ch(
		dom_html_table_row_element *table, dom_string **ch);

dom_exception dom_html_table_row_element_set_ch(
		dom_html_table_row_element *table, dom_string *ch);

dom_exception dom_html_table_row_element_get_ch_off(
		dom_html_table_row_element *table, dom_string **ch_off);

dom_exception dom_html_table_row_element_set_ch_off(
		dom_html_table_row_element *table, dom_string *ch_off);

dom_exception dom_html_table_row_element_get_v_align(
		dom_html_table_row_element *table, dom_string **v_align);

dom_exception dom_html_table_row_element_set_v_align(
		dom_html_table_row_element *table, dom_string *v_align);

dom_exception dom_html_table_row_element_get_row_index(
		dom_html_table_row_element *table, int32_t *index);

dom_exception dom_html_table_row_element_get_section_row_index(
		dom_html_table_row_element *table_row, int32_t *section_row_index);

dom_exception dom_html_table_row_element_get_cells(
		dom_html_table_row_element *element,
		dom_html_collection **cells);

dom_exception dom_html_table_row_element_delete_cell(
		dom_html_table_row_element *element,
		int32_t index);

dom_exception dom_html_table_row_element_insert_cell(
		dom_html_table_row_element *element,
		int32_t index, dom_html_element **cell);

#endif

