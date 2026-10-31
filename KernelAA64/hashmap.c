/**
* @file hashmap.c
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

#include <hashmap.h>
#include <string.h>
#include <_null.h>
#include <Drivers/uart.h>
#include <Mm/kmalloc.h>

unsigned int hashmap_string_hash(const void* _key) {
	unsigned int hash = 0;
	char* key = (char*)_key;
	int c;
	while ((c = *key++))
		hash = c + (hash << 6) + (hash << 16) - hash;
	return hash;
}

int hashmap_string_comp(const void* a, const void* b) {
	return !strcmp((const char*)a, (const char*)b);
}

void* hashmap_string_dupe(const void* key) {
	return strdup((const char*)key);
}

unsigned int hashmap_int_hash(const void* key) {
	return (intptr_t)key;
}

int hashmap_int_comp(const void* a, const void* b) {
	return (intptr_t)a == (intptr_t)b;
}

void* hashmap_int_dupe(const void* key) {
	return (void*)key;
}

static void hashmap_int_free(void* ptr) {
	(void)ptr;
	return;
}

/**
 *AuHashmapCreate -- create a new hashmap
 * @param size -- size of the hashmap
 */
hashmap_t* AuHashmapCreate(int size) {
	hashmap_t* map = (hashmap_t*)kmalloc(sizeof(hashmap_t));
	map->hash_func = hashmap_string_hash;
	map->hash_comp = hashmap_string_comp;
	map->hash_key_dup = hashmap_string_dupe;
	map->hash_key_free = kfree;
	map->hash_val_free = kfree;
	map->size = size;
	map->entries = (hashmap_entry_t**)kmalloc(sizeof(hashmap_entry_t*) * size);
	memset(map->entries, 0, sizeof(hashmap_entry_t*) * size);

	return map;
}

hashmap_t* AuHashmapCreateInt(int size) {
	hashmap_t* map = (hashmap_t*)kmalloc(sizeof(hashmap_t));
	map->hash_func = hashmap_int_hash;
	map->hash_comp = hashmap_int_comp;
	map->hash_key_dup = hashmap_int_dupe;
	map->hash_key_free = hashmap_int_free;
	map->hash_val_free = hashmap_int_free;
	map->size = size;
	map->entries = (hashmap_entry_t**)kmalloc(sizeof(hashmap_entry_t*) * size);
	memset(map->entries, 0, sizeof(hashmap_entry_t*) * size);
	return map;
}

void* AuHashmapSet(hashmap_t* map, const void* key, void* value) {
	unsigned int hash = map->hash_func(key) % map->size;
	UARTDebugOut("[[Hashmap func : %d \r\n", hash);
	hashmap_entry_t* x = map->entries[hash];
	if (!x) {
		hashmap_entry_t* e = (hashmap_entry_t*)kmalloc(sizeof(hashmap_entry_t));
		e->key = (char*)map->hash_key_dup(key);
		e->value = value;
		e->next = NULL;
		map->entries[hash] = e;
		return NULL;
	} else {
		hashmap_entry_t* p = NULL;
		do {
			if (map->hash_comp(x->key, key)) {
				void* out = x->value;
				x->value = value;
				return out;
			} else {
				p = x;
				x = x->next;
			}
		} while (x);
		hashmap_entry_t* e = (hashmap_entry_t*)kmalloc(sizeof(hashmap_entry_t));
		e->key = (char*)map->hash_key_dup(key);
		e->value = value;
		e->next = NULL;
		p->next = e;
		return NULL;
	}
}

void* AuHashmapGet(hashmap_t* map, const void* key) {
	unsigned int hash = map->hash_func(key) % map->size;
	hashmap_entry_t* x = map->entries[hash];
	if (!x) {
		return NULL;
	} else {
		do {
			if (map->hash_comp(x->key, key))
				return x->value;
			x = x->next;
		} while (x);
		return NULL;
	}
}

void* AuHashmapRemove(hashmap_t* map, const void* key) {
	unsigned int hash = map->hash_func(key) % map->size;

	hashmap_entry_t* x = map->entries[hash];
	if (!x)
		return NULL;
	else {
		if (map->hash_comp(x->key, key)) {
			void* out = x->value;
			map->entries[hash] = x->next;
			map->hash_key_free(x->key);
			map->hash_val_free(x);
			return out;
		} else {
			hashmap_entry_t* p = x;
			x = x->next;
			do {
				if (map->hash_comp(x->key, key)) {
					void* out = x->value;
					p->next = x->next;
					map->hash_key_free(x->key);
					map->hash_val_free(x);
					return out;
				}
				p = x;
				x = x->next;
			} while (x);
		}
		return NULL;
	}
}

int AuHashmapHas(hashmap_t* map, const void* key) {
	unsigned int hash = map->hash_func(key) % map->size;
	hashmap_entry_t* x = map->entries[hash];
	if (!x)
		return 0;
	else {
		do {
			if (map->hash_comp(x->key, key))
				return 1;
			x = x->next;
		} while (x);
		return 0;
	}
}

void AuHashmapFree(hashmap_t* map) {
	for (unsigned int i = 0; i < map->size; ++i) {
		hashmap_entry_t *x = map->entries[i], *p;
		while (x) {
			p = x;
			x = x->next;
			map->hash_key_free(p->key);
			map->hash_val_free(p);
		}
	}
	kfree(map->entries);
}

int AuHashmapIsEmpty(hashmap_t* map) {
	for (unsigned int i = 0; i < map->size; ++i) {
		if (map->entries[i])
			return 0;
	}
	return 1;
}