/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_table_element_h_
#define dom_html_table_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include<dom/html/html_element.h>
#include<dom/html/html_tablecaption_element.h>
#include<dom/html/html_tablesection_element.h>
#include<dom/html/html_tablerow_element.h>

typedef struct dom_html_table_element dom_html_table_element;

dom_exception dom_html_table_element_get_caption(
	dom_html_table_element *element, dom_html_table_caption_element **caption);

dom_exception dom_html_table_element_set_caption(
	dom_html_table_element *element, dom_html_table_caption_element *caption);

dom_exception dom_html_table_element_get_t_head(
	dom_html_table_element *element, dom_html_table_section_element **t_head);

dom_exception dom_html_table_element_set_t_head(
		dom_html_table_element *element, dom_html_table_section_element *t_head);

dom_exception dom_html_table_element_get_t_foot(
	dom_html_table_element *element, dom_html_table_section_element **t_foot);

dom_exception dom_html_table_element_set_t_foot(
	dom_html_table_element *element, dom_html_table_section_element *t_foot);

dom_exception dom_html_table_element_get_rows(
		dom_html_table_element *element, dom_html_collection **rows);

dom_exception dom_html_table_element_get_t_bodies(
		dom_html_table_element *element, dom_html_collection **t_bodies);

dom_exception dom_html_table_element_get_align(
		        dom_html_table_element *table, dom_string **align);

dom_exception dom_html_table_element_set_align(
		        dom_html_table_element *table, dom_string *align);

dom_exception dom_html_table_element_get_bg_color(
		        dom_html_table_element *table, dom_string **bg_color);

dom_exception dom_html_table_element_set_bg_color(
		        dom_html_table_element *table, dom_string *bg_color);

dom_exception dom_html_table_element_get_border(
		        dom_html_table_element *table, dom_string **border);

dom_exception dom_html_table_element_set_border(
		        dom_html_table_element *table, dom_string *border);

dom_exception dom_html_table_element_get_cell_padding(
		        dom_html_table_element *table, dom_string **cell_padding);

dom_exception dom_html_table_element_set_cell_padding(
		        dom_html_table_element *table, dom_string *cell_padding);

dom_exception dom_html_table_element_get_cell_spacing(
		        dom_html_table_element *table, dom_string **cell_spacing);

dom_exception dom_html_table_element_set_cell_spacing(
		        dom_html_table_element *table, dom_string *cell_spacing);

dom_exception dom_html_table_element_get_frame(
		        dom_html_table_element *table, dom_string **frame);

dom_exception dom_html_table_element_set_frame(
		        dom_html_table_element *table, dom_string *frame);

dom_exception dom_html_table_element_get_rules(
		        dom_html_table_element *table, dom_string **rules);

dom_exception dom_html_table_element_set_rules(
		        dom_html_table_element *table, dom_string *rules);

dom_exception dom_html_table_element_get_summary(
		        dom_html_table_element *table, dom_string **summary);

dom_exception dom_html_table_element_set_summary(
		        dom_html_table_element *table, dom_string *summary);

dom_exception dom_html_table_element_get_width(
		        dom_html_table_element *table, dom_string **width);

dom_exception dom_html_table_element_set_width(
		        dom_html_table_element *table, dom_string *width);

dom_exception dom_html_table_element_create_caption(
		dom_html_table_element *element,
		dom_html_element **caption);

dom_exception dom_html_table_element_delete_caption(
		dom_html_table_element *element);

dom_exception dom_html_table_element_create_t_head(
		dom_html_table_element *element,
		dom_html_element **t_head);

dom_exception dom_html_table_element_delete_t_head(
		dom_html_table_element *element);

dom_exception dom_html_table_element_create_t_foot(
		dom_html_table_element *element,
		dom_html_element **t_foot);

dom_exception dom_html_table_element_delete_t_foot(
		dom_html_table_element *element);

dom_exception dom_html_table_element_insert_row(
		dom_html_table_element *element,
		int32_t index, dom_html_element **row_out);

dom_exception dom_html_table_element_delete_row(
		dom_html_table_element *element,
		int32_t index);

#endif

