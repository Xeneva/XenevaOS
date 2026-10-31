/*
 * This file is part of libdom.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2022 Daniel Silverstone <dsilvers@digital-scurf.org>
 */

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <dom/core/element.h>
#include <dom/core/nodelist.h>
#include <dom/core/tokenlist.h>
#include <dom/core/string.h>
#include <dom/events/event.h>
#include <dom/events/event_target.h>
#include <dom/events/event_listener.h>
#include <dom/events/mutation_event.h>

#include "core/element.h"
#include "core/document.h"

#include "utils/utils.h"

#define DOM_TOKENLIST_GROW_INCREMENT 4

struct dom_tokenlist {
	uint32_t refcnt;
	dom_element *ele;
	dom_string *attr;
	dom_event_listener *listener;
	dom_string *last_set;
	bool needs_parse;
	/* Parsed content, for optimal access */
	dom_string **entries;
	uint32_t len;
	uint32_t alloc;
};

/* Handle a DOMAttrModified event which might be to do with our attribute */

static void _dom_tokenlist_handle_attrmodified(dom_event *evt, void *pw)
{
	dom_mutation_event *mutevt = (dom_mutation_event *)evt;
	dom_tokenlist *list = (dom_tokenlist *)pw;
	dom_exception exc;
	dom_string *value;

	{
		dom_event_target *target;
		exc = dom_event_get_target(evt, &target);
		if (exc != DOM_NO_ERR)
			return;
		dom_node_unref(target);
		if (target != (dom_event_target *)list->ele)
			return;
	}

	{
		dom_string *attr;
		exc = dom_mutation_event_get_attr_name(mutevt, &attr);
		if (exc != DOM_NO_ERR)
			return;
		if (!dom_string_isequal(attr, list->attr)) {
			dom_string_unref(attr);
			return;
		}
		dom_string_unref(attr);
	}

	/* At this point we know that this is a mutation of our attribute on our
	 * node */

	exc = dom_mutation_event_get_new_value(mutevt, &value);
	if (exc != DOM_NO_ERR)
		return;

	if (list->last_set != NULL &&
	    dom_string_isequal(list->last_set, value)) {
		/* We've just seen the mutation event for one of our own set
		 * operations */
		dom_string_unref(value);
		return;
	}

	/* Mark that we need to re-parse the tokenlist on the next request */
	list->needs_parse = true;

	dom_string_unref(value);
}

static dom_exception _dom_tokenlist_make_room(dom_tokenlist *list)
{
	if (list->len == list->alloc) {
		uint32_t new_alloc = list->alloc + DOM_TOKENLIST_GROW_INCREMENT;
		dom_string **new_entries = realloc(
			list->entries, new_alloc * sizeof(dom_string *));
		if (new_entries == NULL)
			return DOM_NO_MEM_ERR;
		list->alloc = new_alloc;
		list->entries = new_entries;
	}

	return DOM_NO_ERR;
}

static dom_exception _dom_tokenlist_reparse(dom_tokenlist *list)
{
	dom_exception exc;
	dom_string *value;
	const char *pos;
	uint32_t remaining, check;
	uint32_t n_entries = 0;
	dom_string *temp;
	bool found;

	if (!list->needs_parse)
		return DOM_NO_ERR;

	/* Clean down the current entries */
	while (list->len-- > 0)
		dom_string_unref(list->entries[list->len]);
	list->len = 0;

	/* Get the "new" attribute value */
	exc = dom_element_get_attribute(list->ele, list->attr, &value);
	if (exc != DOM_NO_ERR)
		return exc;

	/* If there is no value, we're an empty list and we're done */
	if (value == NULL) {
		list->needs_parse = false;
		return DOM_NO_ERR;
	}

	/* OK, there's something here to do, so let's do it... */

	/* Count number of entries */
	for (pos = dom_string_data(value), remaining = dom_string_length(value);
	     remaining > 0;) {
		if (*pos != ' ') {
			while (*pos != ' ' && remaining > 0) {
				remaining--;
				pos++;
			}
			n_entries++;
		} else {
			while (*pos == ' ' && remaining > 0) {
				remaining--;
				pos++;
			}
		}
	}

	/* If there are no entries (all whitespace) just bail here */
	if (n_entries == 0) {
		list->needs_parse = false;
		dom_string_unref(value);
		return DOM_NO_ERR;
	}

	/* If we need more room, reallocate the buffer */
	if (list->alloc < n_entries) {
		dom_string **new_alloc = realloc(
			list->entries, n_entries * sizeof(dom_string *));
		if (new_alloc == NULL) {
			dom_string_unref(value);
			return DOM_NO_MEM_ERR;
		}
		list->entries = new_alloc;
		list->alloc = n_entries;
	}

	/* And now parse those entries into the buffer */
	for (pos = dom_string_data(value),
	    remaining = dom_string_length(value),
	    n_entries = 0;
	     remaining > 0;) {
		if (*pos != ' ') {
			const char *s = pos;
			while (*pos != ' ' && remaining > 0) {
				pos++;
				remaining--;
			}
			exc = dom_string_create_interned((const uint8_t *)s,
							 pos - s,
							 &temp);
			if (exc != DOM_NO_ERR) {
				dom_string_unref(value);
				return exc;
			}
			found = false;
			for (check = 0; check < list->len; check++) {
				if (dom_string_isequal(temp,
						       list->entries[check])) {
					found = true;
					break;
				}
			}
			if (found == true) {
				dom_string_unref(temp);
			} else {
				list->entries[list->len] = temp;
				list->len++;
			}
		} else {
			while (*pos == ' ' && remaining > 0) {
				pos++;
				remaining--;
			}
		}
	}

	dom_string_unref(value);
	list->needs_parse = false;

	return DOM_NO_ERR;
}

static dom_exception _dom_tokenlist_reify(dom_tokenlist *list)
{
	dom_exception exc;
	uint32_t nchars = 0, n;
	char *buffer, *next;
	dom_string *output;

	if (list->len == 0) {
		if (list->last_set != NULL) {
			dom_string_unref(list->last_set);
		}
		list->last_set = dom_string_ref(
			list->ele->base.owner->_memo_empty);
		return dom_element_set_attribute(list->ele,
						 list->attr,
						 list->last_set);
	}

	for (n = 0; n < list->len; ++n)
		nchars += dom_string_length(list->entries[n]);

	buffer = calloc(1, nchars + list->len);
	if (buffer == NULL)
		return DOM_NO_MEM_ERR;

	for (next = buffer, n = 0; n < list->len; ++n) {
		uint32_t slen = dom_string_length(list->entries[n]);
		memcpy(next, dom_string_data(list->entries[n]), slen);
		next[slen] = ' ';
		next += slen + 1;
	}

	exc = dom_string_create_interned((const uint8_t *)buffer,
					 nchars + list->len - 1,
					 &output);
	free(buffer);
	if (exc != DOM_NO_ERR)
		return exc;

	if (list->last_set != NULL) {
		dom_string_unref(list->last_set);
	}
	list->last_set = output;

	return dom_element_set_attribute(list->ele, list->attr, list->last_set);
}

/**********************************************************************************/

/**
 * Create a tokenlist
 *
 * \param ele  The element which owns the tokenlist attribute
 * \param attr The name of the attribute we are treating as a tokenlist
 * \param list The tokenlist output which is set on success
 * \return DOM_NO_ERR on success, DOM_NO_MEM_ERR on memory exhaustion
 *
 * The returned list will already be referenced, so the client need not
 * do so explicitly. The client must unref the list once finished with it.
 *
 * This list will take its own references to ::ele and ::attr
 */
dom_exception
dom_tokenlist_create(dom_element *ele, dom_string *attr, dom_tokenlist **list)
{
	dom_tokenlist *l;
	dom_exception exc;

	l = calloc(1, sizeof(dom_tokenlist));
	if (l == NULL)
		return DOM_NO_MEM_ERR;

	l->refcnt = 1;
	l->ele = (dom_element *)dom_node_ref(ele);
	l->attr = dom_string_ref(attr);
	l->needs_parse = true;

	exc = dom_event_listener_create(_dom_tokenlist_handle_attrmodified,
					l,
					&l->listener);
	if (exc != DOM_NO_ERR)
		goto fail;

	exc = dom_event_target_add_event_listener(
		ele,
		ele->base.owner->_memo_domattrmodified,
		l->listener,
		false);

	if (exc != DOM_NO_ERR)
		goto fail;

	*list = l;

	return DOM_NO_ERR;

fail:
	if (l->listener != NULL)
		dom_event_listener_unref(l->listener);
	dom_node_unref(l->ele);
	dom_string_unref(l->attr);
	free(l);
	return exc;
}

/**
 * Claim a ref on a tokenlist
 *
 * \param list The tokenlist to claim a ref on
 */
void dom_tokenlist_ref(dom_tokenlist *list)
{
	assert(list != NULL);
	list->refcnt++;
}

/**
 * Release a ref on a tokenlist
 *
 * \param list The list to release the reference of
 *
 * If you release the last ref, this cleans up the tokenlist
 */
void dom_tokenlist_unref(dom_tokenlist *list)
{
	assert(list != NULL);

	if (--list->refcnt > 0)
		return;

	if (list->alloc > 0) {
		while (list->len-- > 0)
			dom_string_unref(list->entries[list->len]);
		free(list->entries);
	}

	dom_event_target_remove_event_listener(
		list->ele,
		list->ele->base.owner->_memo_domattrmodified,
		list->listener,
		false);

	dom_event_listener_unref(list->listener);

	if (list->last_set != NULL)
		dom_string_unref(list->last_set);

	dom_string_unref(list->attr);
	dom_node_unref(list->ele);

	free(list);
}

/**
 * Get the length of the tokenlist
 *
 * \param list The list to get the length of
 * \param length Length of the list outputs here
 * \return DOM_NO_ERR on success, otherwise the failure code
 */
dom_exception dom_tokenlist_get_length(dom_tokenlist *list, uint32_t *length)
{
	dom_exception exc;
	assert(list != NULL);

	exc = _dom_tokenlist_reparse(list);
	if (exc != DOM_NO_ERR)
		return exc;

	*length = list->len;

	return DOM_NO_ERR;
}

/**
 * Get a particular item from the tokenlist
 *
 * \param list The list to retrieve the item from
 * \param index The index of the item to retrieve
 * \param value The value of the item returns here
 * \return DOM_NO_ERR on success, otherwise the failure code
 */
dom_exception
_dom_tokenlist_item(dom_tokenlist *list, uint32_t index, dom_string **value)
{
	dom_exception exc;
	assert(list != NULL);

	exc = _dom_tokenlist_reparse(list);
	if (exc != DOM_NO_ERR)
		return exc;

	if (index >= list->len) {
		*value = NULL;
		return DOM_NO_ERR;
	}

	*value = dom_string_ref(list->entries[index]);
	return DOM_NO_ERR;
}

/**
 * Retrieve the value of the tokenlist as a string
 *
 * \param list The list to retrieve the value of
 * \param value The value of the list returns here
 * \return DOM_NO_ERR on success, otherwise the failure code
 */
dom_exception dom_tokenlist_get_value(dom_tokenlist *list, dom_string **value)
{
	assert(list != NULL);

	return dom_element_get_attribute(list->ele, list->attr, value);
}

/**
 * Set the value of the tokenlist as a string
 *
 * \param list The list to set the value of
 * \param value The value to set
 * \return DOM_NO_ERR on success, otherwise the failure code
 *
 */
dom_exception dom_tokenlist_set_value(dom_tokenlist *list, dom_string *value)
{
	assert(list != NULL);

	return dom_element_set_attribute(list->ele, list->attr, value);
}

/**
 * Check if the given value is in the tokenlist
 *
 * \param list The list to scan for the given value
 * \param value The value to look for in the token list
 * \param contains This will be set based on whether or not the value is present
 * \return DOM_NO_ERR on success, otherwise the failure code
 */
dom_exception
dom_tokenlist_contains(dom_tokenlist *list, dom_string *value, bool *contains)
{
	dom_exception exc;
	uint32_t n;

	assert(list != NULL);

	exc = _dom_tokenlist_reparse(list);
	if (exc != DOM_NO_ERR)
		return exc;

	*contains = false;

	for (n = 0; n < list->len; n++) {
		if (dom_string_isequal(value, list->entries[n])) {
			*contains = true;
			break;
		}
	}

	return DOM_NO_ERR;
}

/**
 * Add the given value to the tokenlist
 *
 * \param list The list to add to
 * \param value The value to add
 * \return DOM_NO_ERR on success, otherwise the failure code
 */
dom_exception dom_tokenlist_add(dom_tokenlist *list, dom_string *value)
{
	dom_exception exc;
	bool present = false;

	assert(list != NULL);

	exc = dom_tokenlist_contains(list, value, &present);
	if (exc != DOM_NO_ERR)
		return exc;

	if (present == true)
		return DOM_NO_ERR;

	exc = _dom_tokenlist_make_room(list);
	if (exc != DOM_NO_ERR)
		return exc;

	list->entries[list->len++] = dom_string_ref(value);

	exc = _dom_tokenlist_reify(list);

	return exc;
}

/**
 * Remove the given value from the tokenlist
 *
 * \param list The list to remove from
 * \param value The value to remove
 * \return DOM_NO_ERR on success, otherwise the failure code
 */
dom_exception dom_tokenlist_remove(dom_tokenlist *list, dom_string *value)
{
	dom_exception exc;
	uint32_t n, m;

	assert(list != NULL);

	exc = _dom_tokenlist_reparse(list);
	if (exc != DOM_NO_ERR)
		return false;

	for (n = 0; n < list->len; ++n) {
		if (dom_string_isequal(value, list->entries[n])) {
			dom_string_unref(list->entries[n]);
			for (m = n + 1; m < list->len; ++m) {
				list->entries[m - 1] = list->entries[m];
			}
			list->len--;
			break;
		}
	}

	exc = _dom_tokenlist_reify(list);

	return exc;
}
