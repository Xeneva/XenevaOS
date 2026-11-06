/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2009 Bo Yang <struggleyb.nku@gmail.com>
 */

#ifndef dom_events_keyboard_event_h_
#define dom_events_keyboard_event_h_

#include <stdbool.h>
#include <dom/core/exceptions.h>
#include <dom/core/string.h>

struct dom_abstract_view;

typedef struct dom_keyboard_event dom_keyboard_event;

typedef enum {
	DOM_KEY_LOCATION_STANDARD = 0,
	DOM_KEY_LOCATION_LEFT = 1,
	DOM_KEY_LOCATION_RIGHT = 2,
	DOM_KEY_LOCATION_NUMPAD = 3
} dom_key_location;

dom_exception _dom_keyboard_event_create(dom_keyboard_event **evt);
#define dom_keyboard_event_create(n) \
		_dom_keyboard_event_create((dom_keyboard_event **) (n))

dom_exception _dom_keyboard_event_get_key(dom_keyboard_event *evt,
		dom_string **key);
#define dom_keyboard_event_get_key(e, i) \
		_dom_keyboard_event_get_key( \
		(dom_keyboard_event *) (e), (dom_string **) (i))

dom_exception _dom_keyboard_event_get_code(dom_keyboard_event *evt,
		dom_string **code);
#define dom_keyboard_event_get_code(e, i) \
		_dom_keyboard_event_get_code( \
		(dom_keyboard_event *) (e), (dom_string **) (i))

dom_exception _dom_keyboard_event_get_location(dom_keyboard_event *evt,
		dom_key_location *location);
#define dom_keyboard_event_get_location(e, l) \
		_dom_keyboard_event_get_location( \
		(dom_keyboard_event *) (e), (dom_key_location *) (l))

dom_exception _dom_keyboard_event_get_ctrl_key(dom_keyboard_event *evt,
		bool *key);
#define dom_keyboard_event_get_ctrl_key(e, k) _dom_keyboard_event_get_ctrl_key(\
		(dom_keyboard_event *) (e), (bool *) (k))

dom_exception _dom_keyboard_event_get_shift_key(dom_keyboard_event *evt,
		bool *key);
#define dom_keyboard_event_get_shift_key(e, k) \
		_dom_keyboard_event_get_shift_key((dom_keyboard_event *) (e), \
		(bool *) (k))

dom_exception _dom_keyboard_event_get_alt_key(dom_keyboard_event *evt,
		bool *key);
#define dom_keyboard_event_get_alt_key(e, k) _dom_keyboard_event_get_alt_key(\
		(dom_keyboard_event *) (e), (bool *) (k))

dom_exception _dom_keyboard_event_get_meta_key(dom_keyboard_event *evt,
		bool *key);
#define dom_keyboard_event_get_meta_key(e, k) _dom_keyboard_event_get_meta_key(\
		(dom_keyboard_event *) (e), (bool *) (k))

dom_exception _dom_keyboard_event_get_modifier_state(dom_keyboard_event *evt,
		dom_string *m, bool *state);
#define dom_keyboard_event_get_modifier_state(e, m, s) \
		_dom_keyboard_event_get_modifier_state( \
		(dom_keyboard_event *) (e), (dom_string *) (m),\
		(bool *) (s))

dom_exception _dom_keyboard_event_init(dom_keyboard_event *evt, 
		dom_string *type, bool bubble, bool cancelable, 
		struct dom_abstract_view *view, dom_string *key,
		dom_string *code, dom_key_location location,
		bool ctrl_key, bool shift_key, bool alt_key, bool meta_key,
		bool repeat, bool is_composing);
#define dom_keyboard_event_init(e, t, b, c, v, kk, kc, kl, ck, sk, ak, mk, r, ic) \
		_dom_keyboard_event_init((dom_keyboard_event *) (e), \
		(dom_string *) (t), (bool) (b), (bool) (c), \
		(struct dom_abstract_view *) (v), (dom_string *) (kk), \
		(dom_string *) (kc), (dom_key_location) (kl), \
		(bool) (ck), (bool) (sk), (bool) (ak), (bool) (mk), \
		(bool) (r), (bool) (ic))

dom_exception _dom_keyboard_event_init_ns(dom_keyboard_event *evt, 
		dom_string *namespace, dom_string *type,
		bool bubble, bool cancelable, struct dom_abstract_view *view,
		dom_string *key, dom_string *code, dom_key_location location,
		bool ctrl_key, bool shift_key, bool alt_key, bool meta_key,
		bool repeat, bool is_composing);
#define dom_keyboard_event_init_ns(e, n, t, b, c, v, ki, kl, m) \
		_dom_keyboard_event_init_ns((dom_keyboard_event *) (e), \
		(dom_string *) (n), (dom_string *) (t), \
		(bool) (b), (bool) (c), \
		(struct dom_abstract_view *) (v), (dom_string *) (kk), \
		(dom_string *) (kc), (dom_key_location) (kl), \
		(bool) (ck), (bool) (sk), (bool) (ak), (bool) (mk), \
		(bool) (r), (bool) (ic))

#endif

