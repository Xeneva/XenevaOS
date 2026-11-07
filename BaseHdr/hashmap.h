/**
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

#ifndef __HASHMAP_H__
#define __HASHMAP_H__

#include <aurora.h>

typedef unsigned int(*hashmap_hash_t)(const void* key);
typedef int(*hashmap_comp_t)(const void *a, const void* b);
typedef void(*hashmap_free_t)(void*);
typedef void*(*hashmap_dupe_t)(const void*);

typedef struct hashmap_entry {
	char* key;
	void* value;
	struct hashmap_entry* next;
}hashmap_entry_t;


typedef struct hashmap {
	hashmap_hash_t hash_func;
	hashmap_comp_t hash_comp;
	hashmap_dupe_t hash_key_dup;
	hashmap_free_t hash_key_free;
	hashmap_free_t hash_val_free;
	size_t size;
	hashmap_entry_t ** entries;
}hashmap_t;

/*
*AuHashmapCreate -- create a new hashmap
* @param size -- size of the hashmap
*/
AU_EXTERN AU_EXPORT hashmap_t* AuHashmapCreate(int size);
AU_EXTERN AU_EXPORT hashmap_t* AuHashmapCreateInt(int size);
AU_EXTERN AU_EXPORT void* AuHashmapSet(hashmap_t* map, const void* key, void* value);
AU_EXTERN AU_EXPORT void* AuHashmapGet(hashmap_t* map, const void* key);
AU_EXTERN AU_EXPORT void* AuHashmapRemove(hashmap_t* map, const void* key);
AU_EXTERN AU_EXPORT int AuHashmapHas(hashmap_t* map, const void* key);
AU_EXTERN AU_EXPORT void AuHashmapFree(hashmap_t *map);
AU_EXTERN AU_EXPORT int AuHashmapIsEmpty(hashmap_t* map);

#endif