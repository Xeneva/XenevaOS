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

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <sys/types.h>

extern char* _argv_0;
extern int __libc_debug;

struct SortableArray {
	void* data;
	size_t size;
	int (*func)(const void*, const void*);
};

static ssize_t partition(struct SortableArray* array, ssize_t lo, ssize_t hi) {
	char* pivot = (char*)malloc(array->size);
	memcpy(pivot, (char*)array->data + array->size * hi, array->size);
	ssize_t i = lo - 1;
	for (ssize_t j = lo; j <= hi; ++j) {
		uint8_t* obj_j = (uint8_t*)array->data + array->size * j;
		if (array->func(obj_j, pivot) <= 0) {
			i++;
			if (j != i) {
				uint8_t* obj_i = (uint8_t*)array->data + array->size * i;
				for (size_t x = 0; x < array->size; ++x) {
					uint8_t tmp = obj_i[x];
					obj_i[x] = obj_j[x];
					obj_j[x] = tmp;
				}
			}
		}
	}
	free(pivot);
	return i;
}

static void quicksort(struct SortableArray* array, ssize_t lo, ssize_t hi) {
	if (lo >= 0 && hi >= 0) {
		if (lo < hi) {
			ssize_t pivot = partition(array, lo, hi);
			quicksort(array, lo, pivot - 1);
			quicksort(array, pivot + 1, hi);
		}
	}
}

void qsort(void* base, size_t nmemb, size_t size, int (*compar)(const void*, const void*)) {
	if (nmemb < 2)
		return;
	if (!size)
		return;
	struct SortableArray array = {base, size, compar};
	quicksort(&array, 0, nmemb - 1);
}
