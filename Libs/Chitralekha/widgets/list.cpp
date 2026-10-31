/**
* @file list.cpp
* 
* BSD 2-Clause License
*
* Copyright (c) 2022-2023, Manas Kamal Choudhury
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice, this
*    list of conditions and the following disclaimer.
*
* 2. Redistributions in binary form must reproduce the above copyright notice,
*    this list of conditions and the following disclaimer in the documentation
*    and/or other materials provided with the distribution.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
**/

#include "list.h"
#include <stdlib.h>

list_t* initialize_list() {
	list_t* list = (list_t*)malloc(sizeof(list_t));
	list->entry_current = nullptr;
	list->pointer = 0;
	return list;
}

void list_add(list_t* list, void* data) {
	dataentry* current_data = (dataentry*)malloc(sizeof(dataentry));
	current_data->next = nullptr;
	current_data->prev = nullptr;
	current_data->data = data;

	if (!list->entry_current) {
		list->entry_current = current_data;
	} else {
		dataentry* current_entry = list->entry_current;
		while (current_entry->next) {
			current_entry = current_entry->next;
		}
		current_entry->next = current_data;
		current_data->prev = current_entry;
	}

	list->pointer++;
}

void* list_get_at(list_t* list, unsigned int index) {
	if (list->pointer == 0 || index >= list->pointer)
		return nullptr;

	dataentry* current_node = list->entry_current;

	for (unsigned int current_index = 0; (current_index < index) && current_node; current_index++)
		current_node = current_node->next;

	return current_node ? current_node->data : nullptr;
}

void* list_remove(list_t* list, unsigned int index) {
	void* payload;

	if (list->pointer == 0 || index >= list->pointer)
		return nullptr;

	dataentry* current_node = list->entry_current;

	for (unsigned int current_index = 0; (current_index < index) && current_node; current_index++)
		current_node = current_node->next;

	if (!current_node)
		return nullptr;
	payload = current_node->data;

	if (current_node->prev)
		current_node->prev->next = current_node->next;

	if (current_node->next)
		current_node->next->prev = current_node->prev;

	if (index == 0)
		list->entry_current = current_node->next;

	free(current_node);

	list->pointer--;

	return payload;
}

/*
 * list_clear_all -- clear all nodes 
 */
void list_clear_all(list_t* list) {
	dataentry* de = list->entry_current;
	dataentry* current = de;
	while (current) {
		de = de->next;
		free(current);
		current = de;
		list->pointer--;
	}
}