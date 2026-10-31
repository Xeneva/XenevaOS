/*
 * This file is part of LibParserUtils.
 * Licensed under the MIT License,
 *                http://www.opensource.org/licenses/mit-license.php
 * Copyright 2008 John-Mark Bell <jmb@netsurf-browser.org>
 */

#include <string.h>
#include <stdarg.h>

#include <parserutils/utils/buffer.h>

#define DEFAULT_SIZE (4096)

/**
 * Create a memory buffer
 *
 * \param buffer  Pointer to location to receive memory buffer
 * \return PARSERUTILS_OK on success,
 *         PARSERUTILS_BADPARM on bad parameters,
 *         PARSERUTILS_NOMEM on memory exhausion
 */
parserutils_error parserutils_buffer_create(parserutils_buffer **buffer)
{
	parserutils_buffer *b;

	if (buffer == NULL)
		return PARSERUTILS_BADPARM;

	b = malloc(sizeof(parserutils_buffer));
	if (b == NULL)
		return PARSERUTILS_NOMEM;

	b->alloc = malloc(DEFAULT_SIZE);
	if (b->alloc == NULL) {
		free(b);
		return PARSERUTILS_NOMEM;
	}

	b->data = b->alloc;
	b->length = 0;
	b->allocated = DEFAULT_SIZE;

	*buffer = b;

	return PARSERUTILS_OK;
}

/**
 * Destroy a memory buffer
 *
 * \param buffer  The buffer to destroy
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_buffer_destroy(parserutils_buffer *buffer)
{
	if (buffer == NULL)
		return PARSERUTILS_BADPARM;

	free(buffer->alloc);
	free(buffer);

	return PARSERUTILS_OK;
}

/**
 * Get current data offset within buffer's allocation.
 *
 * \param[in]  buffer  The buffer object.
 * \return data offset in bytes.
 */
static inline size_t get_offset(parserutils_buffer *buffer)
{
	return buffer->data - buffer->alloc;
}

/**
 * Try moving the data to the start of the allocation.
 *
 * \param[in]  buffer  The buffer object.
 */
static inline void try_rebase(parserutils_buffer *buffer)
{
	if (get_offset(buffer) >= buffer->length) {
		memcpy(buffer->alloc, buffer->data, buffer->length);
		buffer->data = buffer->alloc;
	}
}

/**
 * Ensure that the buffer has enough space at the end to add len bytes.
 *
 * \param[in]  buffer  The buffer object.
 * \param[in]  len     Number of bytes to ensure there is space for.
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
static inline parserutils_error ensure_space(
		parserutils_buffer *buffer,
		size_t len)
{
	try_rebase(buffer);

	while (len >= buffer->allocated - buffer->length - get_offset(buffer)) {
		parserutils_error error = parserutils_buffer_grow(buffer);
		if (error != PARSERUTILS_OK)
			return error;
	}

	return PARSERUTILS_OK;
}

/**
 * Append data to a memory buffer
 *
 * \param buffer  The buffer to append to
 * \param data    The data to append
 * \param len     The length, in bytes, of the data to append
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
parserutils_error parserutils_buffer_append(parserutils_buffer *buffer, 
		const uint8_t *data, size_t len)
{
	parserutils_error error = ensure_space(buffer, len);
	if (error != PARSERUTILS_OK)
		return error;

	memcpy(buffer->data + buffer->length, data, len);

	buffer->length += len;

	return PARSERUTILS_OK;
}

/**
 * Append multiple data blocks to a memory buffer.
 *
 * Each data block must be passed as a pair of const uint8_t* and size_t
 *
 * \param buffer  The buffer to append to
 * \param count   The number of data blocks to append
 * \param ...     The pairs of pointer and size
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
*/
parserutils_error parserutils_buffer_appendv(parserutils_buffer *buffer,
		size_t count, ...)
{
	va_list ap;
	parserutils_error error = PARSERUTILS_OK;
	const uint8_t *data;
	size_t len;

	va_start(ap, count);
	while (count > 0) {
		data = va_arg(ap, const uint8_t *);
		len = va_arg(ap, size_t);
		error = parserutils_buffer_append(buffer, data, len);
		if (error != PARSERUTILS_OK)
			break;
		count--;
	}
	va_end(ap);

	return error;
}

/**
 * Insert data into a memory buffer
 *
 * \param buffer  The buffer to insert into
 * \param offset  The offset into the buffer to insert at
 * \param data    The data to insert
 * \param len     The length, in bytes, of the data to insert
 * \return PARSERUTILS_OK on success, appropriate error otherwise
 */
parserutils_error parserutils_buffer_insert(parserutils_buffer *buffer, 
		size_t offset, const uint8_t *data, size_t len)
{
	parserutils_error error;

	if (offset > buffer->length)
		return PARSERUTILS_BADPARM;

	if (offset == buffer->length)
		return parserutils_buffer_append(buffer, data, len);

	error = ensure_space(buffer, len);
	if (error != PARSERUTILS_OK)
		return error;

	memmove(buffer->data + offset + len,
			buffer->data + offset, buffer->length - offset);

	memcpy(buffer->data + offset, data, len);

	buffer->length += len;

	return PARSERUTILS_OK;
}

/**
 * Discard a section of a memory buffer
 *
 * \param buffer  The buffer to discard data from
 * \param offset  The offset into the buffer of the start of the section
 * \param len     The number of bytes to discard
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
parserutils_error parserutils_buffer_discard(parserutils_buffer *buffer, 
		size_t offset, size_t len)
{
	if (offset >= buffer->length || offset + len > buffer->length)
		return PARSERUTILS_BADPARM;

	if (offset == 0) {
		buffer->data += len;
		buffer->length -= len;
		try_rebase(buffer);
		return PARSERUTILS_OK;
	}

	memmove(buffer->data + offset, buffer->data + offset + len, 
			buffer->length - (len + offset));

	buffer->length -= len;

	return PARSERUTILS_OK;
}

/**
 * Extend the amount of space allocated for a memory buffer
 *
 * \param buffer  The buffer to extend
 * \return PARSERUTILS_OK on success, appropriate error otherwise.
 */
parserutils_error parserutils_buffer_grow(parserutils_buffer *buffer)
{
	size_t offset = get_offset(buffer);
	uint8_t *temp = realloc(buffer->alloc, buffer->allocated * 2);
	if (temp == NULL)
		return PARSERUTILS_NOMEM;

	buffer->alloc = temp;
	buffer->data = buffer->alloc + offset;
	buffer->allocated *= 2;

	return PARSERUTILS_OK;
}

parserutils_error parserutils_buffer_randomise(parserutils_buffer *buffer)
{
#ifndef NDEBUG
	uint8_t *temp;
#endif

	if (buffer == NULL)
		return PARSERUTILS_BADPARM;

#ifndef NDEBUG
	temp = malloc(buffer->allocated);
	if (temp == NULL)
		return PARSERUTILS_NOMEM;

	memcpy(temp, buffer->data, buffer->length);

	memset(buffer->data, 0xff, buffer->length);

	/* Leak the buffer's current data, so we don't reuse it */
	/* buffer->alloc(buffer->data, 0, buffer->pw); */

	buffer->alloc = buffer->data = temp;
#endif


	return PARSERUTILS_OK;
}

