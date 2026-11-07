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

#ifndef __STDIO_H__
#define __STDIO_H__

#include <stdint.h>
#include <_xeneva.h>
#include <stdarg.h>

#ifdef __cplusplus
XE_EXTERN {
#endif

	typedef struct _IO_FILE_ {
		unsigned char* base;
		unsigned char* pos;
		unsigned char* ptr;
		int _file_num;
		int eof;
		int size;
		int curr_pos;
	} FILE;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define stdin  (FILE*)(1)
#define stdout (FILE*)(1 + 1)
#define stderr (FILE*)(2 + 1)

	XE_LIB int fprintf(FILE*, const char*, ...);
	XE_LIB int printf(const char*, ...);

	/*
	* fopen -- opens the file specified by name and associates a
	* stream with it.
	* @param name -- file name
	* @param mode -- file mode
	*/
	XE_LIB FILE* fopen(const char* name, const char* mode);
	/*
	* fread -- reads data from the given stream
	* into the array pointed to by ptr
	* @param ptr -- pointer to a block of mem with
	* size of sz*nmemb
	* @param sz -- the size in bytes of each element
	* to be read
	* @param nmemb -- number of elements, each one
	* with a size of size bytes
	* @param stream -- pointer to a FILE object
	*/
	XE_LIB size_t fread(void* ptr, size_t size, size_t nmemb, FILE* stream);

	/*
	* fwrite -- writes up to count items,
	* each of size bytes in length, from buffer
	* to the output stream
	* @param ptr -- pointer to a block of mem with
	* size of sz*nmemb holding the data
	* @param sz -- the size in bytes of each element
	* to be read
	* @param nmemb -- number of elements, each one
	* with a size of size bytes
	* @param stream -- pointer to a FILE object
	*/
	XE_LIB size_t fwrite(void* ptr, size_t sz, size_t nmemb, FILE* stream);

	/*
	* ftell -- returns the current file position of the
	* specified stream with respect to the starting of
	* the file
	* @param fp -- pointer to FILE structure
	*/
	XE_LIB long ftell(FILE * fp);

	/*
	* fseek -- moves or changes the position of
	* the file pointer which is being used to
	* read the file, to a specified byte
	* offset position
	* @param fp -- pointer to FILE structure
	* @param offset -- offset in bytes
	* @param pos -- position mode
	*/
	XE_LIB int fseek(FILE * fp, long int offset, int pos);

	/*
	* fgetc -- reads a single character from the
	* input stream at the current position
	* and increases the file pointer.
	* @param fp -- pointer to FILE structure
	*/
	XE_LIB int fgetc(FILE * fp);

	/*
	* fclose -- closes a file
	* @param fp -- pointer to FILE structure
	*/
	XE_LIB int fclose(FILE * fp);

	XE_LIB int fflush(FILE * stream);

	XE_LIB int fputc(int c, FILE* stream);

	XE_LIB int fputs(const char* s, FILE* stream);

	XE_LIB int puts(const char* s);

	XE_LIB int vfprintf(FILE * stream, const char* format, va_list arg);
	XE_LIB int vsnprintf(char* output, size_t sz, const char* format, va_list ap);
	XE_LIB int vsprintf(char* output, const char* format, va_list list);
	XE_LIB int vprintf(const char* format, va_list list);

	XE_LIB int sprintf(char* str, const char* string, ...);
	XE_LIB int snprintf(char* output, size_t sz, const char* format, ...);

	XE_LIB int printf(const char* format, ...);
	/* getchar -- read a single character
	* from stdin
	*/
	XE_LIB int getchar();

	XE_LIB int remove(const char* pathname);

	XE_LIB int rename(const char* oldpath, const char* newpath);

	XE_LIB int putchar(int c);

	XE_LIB int scanf(const char* format, ...);

	XE_LIB int sscanf(const char* str, const char* format, ...);
#ifdef __cplusplus
}
#endif

#endif