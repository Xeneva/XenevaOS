/**
* @file list.c
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

#include <list.h>
#include <Mm/kmalloc.h>
#include <aucon.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>

list_t* initialize_list() {
	list_t* list = (list_t*)kmalloc(sizeof(list_t));
	if (!list)
		return NULL;
	list->entry_current = NULL;
	list->pointer = 0;
	return list;
}

void list_add(list_t* list, void* data) {
	if (!list)
		return;
	dataentry* current_data = (dataentry*)kmalloc(sizeof(dataentry));
	if (!current_data)
		return;
	current_data->next = NULL;
	current_data->prev = NULL;
	current_data->data = data;

	/*dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();*/

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
	/*dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();*/

	list->pointer++;
	/*aa64_data_cache_clean_range(current_data, sizeof(dataentry));
	aa64_data_cache_clean_range(list, sizeof(list_t));*/
}

void* list_get_at(list_t* list, unsigned int index) {
	if (!list || list->pointer == 0 || index >= list->pointer)
		return NULL;

	/*dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();*/
	dataentry* current_node = list->entry_current;

	for (unsigned int current_index = 0; (current_index < index) && current_node; current_index++) {
		current_node = current_node->next;
		//dmb_sy();
	}

	return current_node ? current_node->data : NULL;
}

void* list_remove(list_t* list, unsigned int index) {
	void* payload;

	if (!list || list->pointer == 0 || index >= list->pointer)
		return NULL;

	dataentry* current_node = list->entry_current;

	for (unsigned int current_index = 0; (current_index < index) && current_node; current_index++)
		current_node = current_node->next;

	if (!current_node)
		return NULL;
	payload = current_node->data;

	if (current_node->prev)
		current_node->prev->next = current_node->next;

	if (current_node->next)
		current_node->next->prev = current_node->prev;

	if (index == 0)
		list->entry_current = current_node->next;

	dmb_sy();
	dmb_ish();
	dsb_sy_barrier();
	dsb_ish();

	kfree(current_node);

	list->pointer--;

	aa64_data_cache_clean_range(list, sizeof(list_t));

	return payload;
}