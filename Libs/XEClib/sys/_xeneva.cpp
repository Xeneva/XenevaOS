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

#include <stdlib.h>
#include <sys/_keproc.h>
#include <string.h>
#include <stdio.h>

extern "C" int _fltused = 1;

//extern "C" void* __cdecl ::operator new(size_t size) {
//	return malloc(size);
//}
//
//extern "C" void* __cdecl ::operator new[](size_t size) {
//	return malloc(size);
//}
//
//extern "C" void __cdecl ::operator delete (void* p) {
//	free(p);
//}
//
//extern "C" void __cdecl ::operator delete[](void* p) {
//	free(p);
//}
//
//extern "C" void __cdecl ::operator delete(void* p, uint64_t s) {
//
//}

XE_EXTERN XE_LIB void _XESetEnvironmentVariable(char* key, char* value, bool overwrite) {
	uint64_t* envptr = (uint64_t*)_KeGetEnvironmentBlock();
	if (!envptr)
		return;
	char* envp = (char*)envptr;
	size_t keyLen = strlen(key);
	size_t valueLen = strlen(value);
	size_t totalLen = keyLen + 1 + valueLen;

	size_t maxEnvSize = 4096;

	char temp[4096];
	memset(temp, 0, sizeof(temp));
	char* dest = temp;
	int found = 0;

	char* scan = envp;
	while (*scan) {
		if (strncmp(scan, key, keyLen) == 0 && scan[keyLen] == '=') {
			found = 1;
			if (!overwrite) {
				// Variable already exists and overwrite is false, do nothing
				return;
			}
			// Skip this variable since we'll replace it
			scan += strlen(scan) + 1;
			continue;
		}

		size_t len = strlen(scan) + 1;
		if ((dest - temp) + len >= maxEnvSize)
			return;
		memcpy(dest, scan, len);
		dest += len;
		scan += len;
	}

	if ((dest - temp) + totalLen + 2 >= maxEnvSize)
		return;

	// Add the new key-value pair
	snprintf(dest, totalLen + 2, "%s=%s", key, value);
	dest[totalLen + 1] = '\0';

	// Copy the complete environment back
	memcpy(envp, temp, dest - temp + 1);
}

XE_EXTERN XE_LIB int _XEPutEnvironmentVariable(char* keyval) {
	uint64_t* envptr = (uint64_t*)_KeGetEnvironmentBlock();
	if (!envptr)
		return NULL;
	char* env = (char*)envptr;
	size_t maxEnvSize = 4096;

	char* eq = strchr(keyval, '=');
	if (!eq)
		return -1;

	size_t keyLen = eq - keyval;

	char* scan = env;
	while (*scan) {
		if (strncmp(scan, keyval, keyLen) == 0 && scan[keyLen] == '=') {
			size_t remain = strlen(scan) + 1;
			memmove(scan, scan + remain, maxEnvSize - (scan - env) - remain);
			break;
		}
		scan += strlen(scan) + 1;
	}

	scan = env;
	while (*scan)
		scan += strlen(scan) + 1;

	size_t newLen = strlen(keyval) + 1;
	if ((scan - env) + newLen + 1 >= maxEnvSize)
		return -1;
	memcpy(scan, keyval, newLen);
	scan[newLen] = '\0';
	return 0;
}

XE_EXTERN XE_LIB const char* _XEGetEnvironmentVariable(const char* key) {
	uint64_t* envptr = (uint64_t*)_KeGetEnvironmentBlock();
	if (!envptr)
		return NULL;
	/* 1: check if the variable already exist*/
	char* envp = (char*)envptr;
	size_t keyLen = strlen(key);
	while (*envp) {
		if (strncmp(envp, key, keyLen) == 0 && envp[keyLen] == '=') {
			return envp + keyLen + 1;
		}

		while (*envp++) {}
	}
	return NULL;
}