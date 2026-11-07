/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#ifndef dom_html_object_element_h_
#define dom_html_object_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

#include <dom/html/html_form_element.h>
#include <dom/html/html_document.h>

typedef struct dom_html_object_element dom_html_object_element;

dom_exception dom_html_object_element_get_form(
	dom_html_object_element *ele, dom_html_form_element **form);

dom_exception dom_html_object_element_get_code(
	dom_html_object_element *object, dom_string **code);

dom_exception dom_html_object_element_set_code(
	dom_html_object_element *object, dom_string *code);

dom_exception dom_html_object_element_get_align(
	dom_html_object_element *object, dom_string **align);

dom_exception dom_html_object_element_set_align(
	dom_html_object_element *object, dom_string *align);

dom_exception dom_html_object_element_get_archive(
	dom_html_object_element *object, dom_string **archive);

dom_exception dom_html_object_element_set_archive(
	dom_html_object_element *object, dom_string *archive);

dom_exception dom_html_object_element_get_border(
	dom_html_object_element *object, dom_string **border);

dom_exception dom_html_object_element_set_border(
	dom_html_object_element *object, dom_string *border);

dom_exception dom_html_object_element_get_code_base(
	dom_html_object_element *object, dom_string **code_base);

dom_exception dom_html_object_element_set_code_base(
	dom_html_object_element *object, dom_string *code_base);

dom_exception dom_html_object_element_get_code_type(
	dom_html_object_element *object, dom_string **code_type);

dom_exception dom_html_object_element_set_code_type(
	dom_html_object_element *object, dom_string *code_type);

dom_exception dom_html_object_element_get_data(
	dom_html_object_element *object, dom_string **data);

dom_exception dom_html_object_element_set_data(
	dom_html_object_element *object, dom_string *data);

dom_exception dom_html_object_element_get_declare(
	dom_html_object_element *ele, bool *declare);

dom_exception dom_html_object_element_set_declare(
	dom_html_object_element *ele, bool declare);

dom_exception dom_html_object_element_get_height(
	dom_html_object_element *object, dom_string **height);

dom_exception dom_html_object_element_set_height(
	dom_html_object_element *object, dom_string *height);

dom_exception dom_html_object_element_get_name(
	dom_html_object_element *object, dom_string **name);

dom_exception dom_html_object_element_set_name(
	dom_html_object_element *object, dom_string *name);

dom_exception dom_html_object_element_get_standby(
	dom_html_object_element *object, dom_string **standby);

dom_exception dom_html_object_element_set_standby(
	dom_html_object_element *object, dom_string *standby);

dom_exception dom_html_object_element_get_type(
	dom_html_object_element *object, dom_string **type);

dom_exception dom_html_object_element_set_type(
	dom_html_object_element *object, dom_string *type);

dom_exception dom_html_object_element_get_use_map(
	dom_html_object_element *object, dom_string **use_map);

dom_exception dom_html_object_element_set_use_map(
	dom_html_object_element *object, dom_string *use_map);

dom_exception dom_html_object_element_get_width(
	dom_html_object_element *object, dom_string **width);

dom_exception dom_html_object_element_set_width(
	dom_html_object_element *object, dom_string *width);

dom_exception dom_html_object_element_get_hspace(
	dom_html_object_element *object, int32_t *hspace);

dom_exception dom_html_object_element_set_hspace(
	dom_html_object_element *object, uint32_t hspace);

dom_exception dom_html_object_element_get_tab_index(
	dom_html_object_element *object, int32_t *tab_index);

dom_exception dom_html_object_element_set_tab_index(
	dom_html_object_element *object, uint32_t tab_index);

dom_exception dom_html_object_element_get_vspace(
	dom_html_object_element *object, int32_t *vspace);

dom_exception dom_html_object_element_set_vspace(
	dom_html_object_element *object, uint32_t vspace);

dom_exception dom_html_object_element_get_content_document(
	dom_html_object_element *object, dom_document **content_document);
#endif
