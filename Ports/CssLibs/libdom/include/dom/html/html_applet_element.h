/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */

#ifndef dom_html_applet_element_h_
#define dom_html_applet_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_applet_element dom_html_applet_element;

dom_exception dom_html_applet_element_get_align(
	dom_html_applet_element *applet, dom_string **align);

dom_exception dom_html_applet_element_set_align(
	dom_html_applet_element *applet, dom_string *align);

dom_exception dom_html_applet_element_get_alt(
	dom_html_applet_element *applet, dom_string **alt);

dom_exception dom_html_applet_element_set_alt(
	dom_html_applet_element *applet, dom_string *alt);

dom_exception dom_html_applet_element_get_archive(
	dom_html_applet_element *applet, dom_string **archive);

dom_exception dom_html_applet_element_set_archive(
	dom_html_applet_element *applet, dom_string *archive);

dom_exception dom_html_applet_element_get_code(
	dom_html_applet_element *applet, dom_string **code);

dom_exception dom_html_applet_element_set_code(
	dom_html_applet_element *applet, dom_string *code);

dom_exception dom_html_applet_element_get_code_base(
	dom_html_applet_element *applet, dom_string **code_base);

dom_exception dom_html_applet_element_set_code_base(
	dom_html_applet_element *applet, dom_string *code_base);

dom_exception dom_html_applet_element_get_height(
	dom_html_applet_element *applet, dom_string **height);

dom_exception dom_html_applet_element_set_height(
	dom_html_applet_element *applet, dom_string *height);

dom_exception dom_html_applet_element_get_name(
	dom_html_applet_element *applet, dom_string **name);

dom_exception dom_html_applet_element_set_name(
	dom_html_applet_element *applet, dom_string *name);

dom_exception dom_html_applet_element_get_object(
	dom_html_applet_element *applet, dom_string **object);

dom_exception dom_html_applet_element_set_object(
	dom_html_applet_element *applet, dom_string *object);

dom_exception dom_html_applet_element_get_width(
	dom_html_applet_element *applet, dom_string **width);

dom_exception dom_html_applet_element_set_width(
	dom_html_applet_element *applet, dom_string *width);


dom_exception dom_html_applet_element_get_hspace(
	dom_html_applet_element *applet, int32_t *hspace);

dom_exception dom_html_applet_element_set_hspace(
	dom_html_applet_element *applet, uint32_t hspace);

dom_exception dom_html_applet_element_get_vspace(
	dom_html_applet_element *applet, int32_t *vspace);

dom_exception dom_html_applet_element_set_vspace(
	dom_html_applet_element *applet, uint32_t vspace);
#endif

