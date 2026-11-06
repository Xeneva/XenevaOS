/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 * Copyright 2014 Rupinder Singh Khokhar <rsk1coder99@gmail.com>
 */
#ifndef dom_html_frame_set_element_h_
#define dom_html_frame_set_element_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

typedef struct dom_html_frame_set_element dom_html_frame_set_element;

dom_exception dom_html_frame_set_element_get_rows(
	dom_html_frame_set_element *element, dom_string **rows);

dom_exception dom_html_frame_set_element_set_rows(
	dom_html_frame_set_element *element, dom_string *rows);

dom_exception dom_html_frame_set_element_set_cols(
	dom_html_frame_set_element *ele, dom_string *cols);

dom_exception dom_html_frame_set_element_get_cols(
	dom_html_frame_set_element *ele, dom_string **cols);

#endif
