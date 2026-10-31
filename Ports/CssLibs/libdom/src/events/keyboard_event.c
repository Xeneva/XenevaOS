/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#include <stdlib.h>
#include <string.h>

#include "events/keyboard_event.h"
#include "core/document.h"

#include "utils/utils.h"

static void _virtual_dom_keyboard_event_destroy(struct dom_event *evt);

static const struct dom_event_private_vtable _event_vtable = {
	_virtual_dom_keyboard_event_destroy
};

/* Constructor */
dom_exception _dom_keyboard_event_create(struct dom_keyboard_event **evt)
{
	*evt = calloc(1, sizeof(dom_keyboard_event));
	if (*evt == NULL) 
		return DOM_NO_MEM_ERR;
	
	((struct dom_event *) *evt)->vtable = &_event_vtable;

	return _dom_keyboard_event_initialise(*evt);
}

/* Destructor */
void _dom_keyboard_event_destroy(struct dom_keyboard_event *evt)
{
	_dom_keyboard_event_finalise(evt);

	free(evt);
}

/* Initialise function */
dom_exception _dom_keyboard_event_initialise(struct dom_keyboard_event *evt)
{
	dom_exception err;
	dom_string *empty_string;

	err = dom_string_create((const uint8_t *)"", 0, &empty_string);
	if (err != DOM_NO_ERR) {
		return err;
	}

	evt->key = empty_string;
	evt->code = dom_string_ref(empty_string);

	return _dom_ui_event_initialise(&evt->base);
}

/* Finalise function */
void _dom_keyboard_event_finalise(struct dom_keyboard_event *evt)
{
	if (evt->key != NULL)
		dom_string_unref(evt->key);
	if (evt->code != NULL)
		dom_string_unref(evt->code);

	_dom_ui_event_finalise(&evt->base);
}

/* The virtual destroy function */
void _virtual_dom_keyboard_event_destroy(struct dom_event *evt)
{
	_dom_keyboard_event_destroy((dom_keyboard_event *) evt);
}

/*----------------------------------------------------------------------*/
/* The public API */

/**
 * Get the key
 *
 * \param evt  The Event object
 * \param key  The returned key
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_key(dom_keyboard_event *evt,
		dom_string **key)
{
	*key = dom_string_ref(evt->key);

	return DOM_NO_ERR;
}

/**
 * Get the code
 *
 * \param evt   The Event object
 * \param code  The returned code
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_code(dom_keyboard_event *evt,
		dom_string **code)
{
	*code = dom_string_ref(evt->code);

	return DOM_NO_ERR;
}

/**
 * Get the key location
 *
 * \param evt  The Event object
 * \param location  The returned key location
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_location(dom_keyboard_event *evt,
		dom_key_location *location)
{
	*location = evt->location;

	return DOM_NO_ERR;
}

/**
 * Get the ctrl key state
 *
 * \param evt  The Event object
 * \param key  Whether the Control key is pressed down
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_ctrl_key(dom_keyboard_event *evt,
		bool *key)
{
	*key = ((evt->modifier_state & DOM_MOD_CTRL) != 0);

	return DOM_NO_ERR;
}

/**
 * Get the shift key state
 *
 * \param evt  The Event object
 * \param key  Whether the Shift key is pressed down
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_shift_key(dom_keyboard_event *evt,
		bool *key)
{
	*key = ((evt->modifier_state & DOM_MOD_SHIFT) != 0);

	return DOM_NO_ERR;
}

/**
 * Get the alt key state
 *
 * \param evt  The Event object
 * \param key  Whether the Alt key is pressed down
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_alt_key(dom_keyboard_event *evt,
		bool *key)
{
	*key = ((evt->modifier_state & DOM_MOD_ALT) != 0);

	return DOM_NO_ERR;
}

/**
 * Get the meta key state
 *
 * \param evt  The Event object
 * \param key  Whether the Meta key is pressed down
 * \return DOM_NO_ERR.
 */
dom_exception _dom_keyboard_event_get_meta_key(dom_keyboard_event *evt,
		bool *key)
{
	*key = ((evt->modifier_state & DOM_MOD_META) != 0);

	return DOM_NO_ERR;
}

/**
 * Query the state of a modifier using a key identifier
 *
 * \param evt    The event object
 * \param ml     The modifier identifier, such as "Alt", "Control", "Meta", 
 *               "AltGraph", "CapsLock", "NumLock", "Scroll", "Shift".
 * \param state  Whether the modifier key is pressed
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 *
 * @note: If an application wishes to distinguish between right and left 
 * modifiers, this information could be deduced using keyboard events and 
 * KeyboardEvent.keyLocation.
 */
dom_exception _dom_keyboard_event_get_modifier_state(dom_keyboard_event *evt,
		dom_string *m, bool *state)
{
	const char *data;
	size_t len;

	if (m == NULL) {
		*state = false;
		return DOM_NO_ERR;
	}

	data = dom_string_data(m);
	len = dom_string_byte_length(m);

	if (len == SLEN("AltGraph") && strncmp(data, "AltGraph", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_ALT_GRAPH) != 0);
	} else if (len == SLEN("Alt") && strncmp(data, "Alt", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_ALT) != 0);
	} else if (len == SLEN("CapsLock") &&
			strncmp(data, "CapsLock", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_CAPS_LOCK) != 0);
	} else if (len == SLEN("Control") &&
			strncmp(data, "Control", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_CTRL) != 0);
	} else if (len == SLEN("Meta") && strncmp(data, "Meta", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_META) != 0);
	} else if (len == SLEN("NumLock") &&
			strncmp(data, "NumLock", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_NUM_LOCK) != 0);
	} else if (len == SLEN("Scroll") &&
			strncmp(data, "Scroll", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_SCROLL) != 0);
	} else if (len == SLEN("Shift") && strncmp(data, "Shift", len) == 0) {
		*state = ((evt->modifier_state & DOM_MOD_SHIFT) != 0);
	}

	return DOM_NO_ERR;
}

/**
 * Helper to initialise the keyboard event
 *
 * \param evt           The Event object
 * \param view          The AbstractView associated with this event
 * \param key           The key identifier of pressed key
 * \param code          The code identifier of pressed key
 * \param location      The key location of the preesed key
 * \param ctrl_key      Whether the ctrl_key is pressed
 * \param shift_key     Whether the shift_key is pressed
 * \param alt_key       Whether the alt_key is pressed
 * \param meta_key      Whether the ctrl_key is pressed
 * \param repeat        Whether this is a repeat press from a held key
 * \param is_composing  Whether the input is being composed
 */
static void _dom_keyboard_event_init_helper(
		dom_keyboard_event *evt,
		dom_string *key,
		dom_string *code,
		dom_key_location location,
		bool ctrl_key,
		bool shift_key,
		bool alt_key,
		bool meta_key,
		bool repeat,
		bool is_composing)
{
	if (key != NULL) {
		dom_string_unref(evt->key);
		evt->key = dom_string_ref(key);
	}
	if (code != NULL) {
		dom_string_unref(evt->code);
		evt->code = dom_string_ref(code);
	}

	evt->location = location;

	if (ctrl_key) {
		evt->modifier_state |= DOM_MOD_CTRL;
	}
	if (shift_key) {
		evt->modifier_state |= DOM_MOD_CTRL;
	}
	if (alt_key) {
		evt->modifier_state |= DOM_MOD_SHIFT;
	}
	if (meta_key) {
		evt->modifier_state |= DOM_MOD_META;
	}

	evt->repeat = repeat;
	evt->is_composing = is_composing;
}



/**
 * Initialise the keyboard event with namespace
 *
 * \param evt           The Event object
 * \param type          The event's type
 * \param bubble        Whether this is a bubbling event
 * \param cancelable    Whether this is a cancelable event
 * \param view          The AbstractView associated with this event
 * \param key           The key identifier of pressed key
 * \param code          The code identifier of pressed key
 * \param location      The key location of the preesed key
 * \param ctrl_key      Whether the ctrl_key is pressed
 * \param shift_key     Whether the shift_key is pressed
 * \param alt_key       Whether the alt_key is pressed
 * \param meta_key      Whether the ctrl_key is pressed
 * \param repeat        Whether this is a repeat press from a held key
 * \param is_composing  Whether the input is being composed
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_keyboard_event_init(
		dom_keyboard_event *evt,
		dom_string *type,
		bool bubble,
		bool cancelable,
		struct dom_abstract_view *view,
		dom_string *key,
		dom_string *code,
		dom_key_location location,
		bool ctrl_key,
		bool shift_key,
		bool alt_key,
		bool meta_key,
		bool repeat,
		bool is_composing)
{
	_dom_keyboard_event_init_helper(evt, key, code, location,
			ctrl_key, shift_key, alt_key, meta_key,
			repeat, is_composing);

	return _dom_ui_event_init(&evt->base, type, bubble, cancelable,
			view, 0);
}

/**
 * Initialise the keyboard event with namespace
 *
 * \param evt           The Event object
 * \param namespace     The namespace of this event
 * \param type          The event's type
 * \param bubble        Whether this is a bubbling event
 * \param cancelable    Whether this is a cancelable event
 * \param view          The AbstractView associated with this event
 * \param key           The key identifier of pressed key
 * \param code          The code identifier of pressed key
 * \param location      The key location of the preesed key
 * \param ctrl_key      Whether the ctrl_key is pressed
 * \param shift_key     Whether the shift_key is pressed
 * \param alt_key       Whether the alt_key is pressed
 * \param meta_key      Whether the ctrl_key is pressed
 * \param repeat        Whether this is a repeat press from a held key
 * \param is_composing  Whether the input is being composed
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_keyboard_event_init_ns(
		dom_keyboard_event *evt,
		dom_string *namespace,
		dom_string *type,
		bool bubble,
		bool cancelable,
		struct dom_abstract_view *view,
		dom_string *key,
		dom_string *code,
		dom_key_location location,
		bool ctrl_key,
		bool shift_key,
		bool alt_key,
		bool meta_key,
		bool repeat,
		bool is_composing)
{
	_dom_keyboard_event_init_helper(evt, key, code, location,
			ctrl_key, shift_key, alt_key, meta_key,
			repeat, is_composing);

	return _dom_ui_event_init_ns(&evt->base, namespace, type, bubble,
			cancelable, view, 0);
}


/*-------------------------------------------------------------------------*/

/**
 * Parse the modifier list string to corresponding bool variable state
 *
 * \param modifier_list   The modifier list string
 * \param modifier_state  The returned state
 * \return DOM_NO_ERR on success, appropriate dom_exception on failure.
 */
dom_exception _dom_parse_modifier_list(dom_string *modifier_list,
		uint32_t *modifier_state)
{
	const char *data;
	const char *m;
	size_t len = 0;

	*modifier_state = 0;

	if (modifier_list == NULL)
		return DOM_NO_ERR;
	
	data = dom_string_data(modifier_list);
	m = data;

	while (true) {
		/* If we reach a space or end of the string, we should parse
		 * the new token. */
		if (*data == ' ' || *data == '\0') {
			if (len == SLEN("AltGraph") &&
					strncmp(m, "AltGraph", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_ALT_GRAPH;
			} else if (len == SLEN("Alt") &&
					strncmp(m, "Alt", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_ALT;
			} else if (len == SLEN("CapsLock") &&
					strncmp(m, "CapsLock", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_CAPS_LOCK;
			} else if (len == SLEN("Control") &&
					strncmp(m, "Control", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_CTRL;
			} else if (len == SLEN("Meta") &&
					strncmp(m, "Meta", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_META;
			} else if (len == SLEN("NumLock") &&
					strncmp(m, "NumLock", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_NUM_LOCK;
			} else if (len == SLEN("Scroll") &&
					strncmp(m, "Scroll", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_SCROLL;
			} else if (len == SLEN("Shift") &&
					strncmp(m, "Shift", len) == 0) {
				*modifier_state = *modifier_state | 
						DOM_MOD_SHIFT;
			}

			while (*data == ' ') {
				data++;
			}
			/* Finished parsing and break */
			if (*data == '\0')
				break;

			m = data;
			len = 0;
		}

		data++;
		len++;
	}

	return DOM_NO_ERR;
}

