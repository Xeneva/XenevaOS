/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#ifndef dom_interntal_events_keyboard_event_h_
#define dom_interntal_events_keyboard_event_h_

#include <dom/events/keyboard_event.h>

#include "events/ui_event.h"

/**
 * The keyboard event
 */
struct dom_keyboard_event {
	struct dom_ui_event base;	/**< The base class */

	dom_string *key;
	dom_string *code;
	dom_key_location location;

	uint32_t modifier_state;	/**< The modifier keys state */

	bool repeat;
	bool is_composing;
};

/* Destructor */
void _dom_keyboard_event_destroy(struct dom_keyboard_event *evt);

/* Initialise function */
dom_exception _dom_keyboard_event_initialise(struct dom_keyboard_event *evt);

/* Finalise function */
void _dom_keyboard_event_finalise(struct dom_keyboard_event *evt);


/* Parse the modifier list string to corresponding bool variable state */
dom_exception _dom_parse_modifier_list(dom_string *modifier_list,
		uint32_t *modifier_state);

#endif
