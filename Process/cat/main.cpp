/**
* BSD 2-Clause License
*
* Copyright (c) 2024, Manas Kamal Choudhury
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
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>
#include <sys/iocodes.h>
#include <string.h>
#include <sys/_ketime.h>
#include <time.h>
#include <stdlib.h>

/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	char* file = argv[1];
	if (!file) {
		printf("cat: no filename specified \n");
		return 1;
	}

	char* filename = (char*)malloc(strlen(file) + 1);
	if (!strchr(file, '/')) {
		sprintf(filename, "/%s", file);
	} else
		sprintf(filename, "%s", file);

	int f = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
	if (f == -1) {
		/* once again try in, using PWD */
		char* pwd = getenv("PWD");
		int totalLen = strlen(filename) + strlen(pwd);
		free(filename);
		filename = (char*)malloc(totalLen);
		memset(filename, 0, totalLen);
		if (!strchr(file, '/'))
			sprintf(filename, "%s/%s", pwd, file);
		else
			sprintf(filename, "%s%s", pwd, file);
		f = _KeOpenFile(filename, FILE_OPEN_READ_ONLY);
		if (f == -1) {
			printf("\nNo file found %s \n", filename);
			return 1;
		}
	}

	XEFileStatus stat;
	_KeFileStat(f, &stat);
	char* buffer = (char*)malloc(stat.size);
	memset(buffer, 0, stat.size);

	size_t read_bytes = _KeReadFile(f, buffer, stat.size);
	printf("\n");
	if (read_bytes > 0) {
		printf("%s", buffer);
	}
	free(filename);
	free(buffer);
	return 0;
}
