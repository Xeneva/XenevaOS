/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_param_element_h_
#define dom_html_param_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_param_element dom_html_param_element;

dom_exception dom_html_param_element_get_name(
	dom_html_param_element *element, dom_string **name);

dom_exception dom_html_param_element_set_name(
	dom_html_param_element *element, dom_string *name);

dom_exception dom_html_param_element_get_type(
	dom_html_param_element *element, dom_string **type);

dom_exception dom_html_param_element_set_type(
	dom_html_param_element *ele, dom_string *type);

dom_exception dom_html_param_element_get_value(
	dom_html_param_element *ele, dom_string **value);

dom_exception dom_html_param_element_set_value(
	dom_html_param_element *ele, dom_string *value);

dom_exception dom_html_param_element_get_value_type(
	dom_html_param_element *ele, dom_string **value_type);

dom_exception dom_html_param_element_set_value_type(
	dom_html_param_element *ele, dom_string *value_type);

#endif
