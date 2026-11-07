/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1v_alignr99@gmail.com>
 */

#ifndef dom_html_table_section_element_h_
#define dom_html_table_section_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include <dom/html/html_collection.h>
#include <dom/html/html_element.h>

#include <dom/html/html_tablerow_element.h>

typedef struct dom_html_table_section_element dom_html_table_section_element;

dom_exception dom_html_table_section_element_get_align(
	dom_html_table_section_element *table_section, dom_string **align);

dom_exception dom_html_table_section_element_set_align(
	dom_html_table_section_element *table_section, dom_string *align);

dom_exception dom_html_table_section_element_get_ch(
	dom_html_table_section_element *table_section, dom_string **ch);

dom_exception dom_html_table_section_element_set_ch(
	dom_html_table_section_element *table_section, dom_string *ch);

dom_exception dom_html_table_section_element_get_ch_off(
	dom_html_table_section_element *table_section, dom_string **ch_off);

dom_exception dom_html_table_section_element_set_ch_off(
	dom_html_table_section_element *table_section, dom_string *ch_off);

dom_exception dom_html_table_section_element_get_v_align(
	dom_html_table_section_element *table_section, dom_string **v_align);

dom_exception dom_html_table_section_element_set_v_align(
	dom_html_table_section_element *table_section, dom_string *v_align);

dom_exception dom_html_table_section_element_get_rows(
	dom_html_table_section_element *table_section, dom_html_collection **rows);

dom_exception dom_html_table_section_element_insert_row(
		dom_html_table_section_element *element,
		int32_t index, dom_html_element **new_row);

dom_exception dom_html_table_section_element_delete_row(
		dom_html_table_section_element *element,
		int32_t index);

#endif

