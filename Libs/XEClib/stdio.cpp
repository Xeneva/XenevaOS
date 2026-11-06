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

#include <stdio.h>
#include <sys/_kefile.h>
#include <string.h>
#include <stdlib.h>
#include <_xeprint.h>

static char output_printf[512];
static size_t output_printf_len = 0;
/*
 * fopen -- opens the file specified by name and associates a
 * stream with it.
 * @param name -- file name
 * @param mode -- file mode
 */
FILE* fopen(const char* name, const char* mode) {
	int mode_ = 0;
	if (strcmp(mode, "r") == 0 || strcmp(mode, "rb") == 0)
		mode_ |= FILE_OPEN_READ_ONLY;
	else if (strcmp(mode, "w") == 0)
		mode_ |= FILE_OPEN_WRITE;
	else if (strcmp(mode, "a") == 0 || strcmp(mode, "ab") == 0)
		mode_ |= FILE_OPEN_WRITE | FILE_OPEN_CREAT;
	else if (strcmp(mode, "r+") == 0 || strcmp(mode, "rb+") == 0
	         || strcmp(mode, "r+b") == 0)
		mode_ |= FILE_OPEN_WRITE;
	else if (strcmp(mode, "w+") == 0 || strcmp(mode, "wb+") == 0
	         || strcmp(mode, "w+b") == 0)
		mode_ |= FILE_OPEN_WRITE | FILE_OPEN_CREAT;
	else if (strcmp(mode, "wb") == 0)
		mode_ |= FILE_OPEN_WRITE | FILE_OPEN_CREAT;

	if (mode_ == 0)
		mode_ |= FILE_OPEN_WRITE | FILE_OPEN_CREAT;

	char filename[32];
	memset(filename, 0, 32);
	int index = 0;
	if (name[0] != '/') {
		filename[index] = '/';
		index++;
	}
	strcpy(filename + index, name);
	int fd = _KeOpenFile(filename, mode_);
	FILE* file = (FILE*)malloc(sizeof(FILE));
	memset(file, 0, sizeof(FILE));
	if (fd == -1)
		return NULL;

	file->_file_num = fd;
	XEFileStatus stat;
	_KeFileStat(file->_file_num, &stat);
	file->size = stat.size;
	return file;
}

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
size_t fread(void* ptr, size_t sz, size_t nmemb, FILE* stream) {
	if (stream->base == NULL) {
		stream->base = (unsigned char*)ptr;
		stream->ptr = stream->base;
	}
	size_t _length = sz * nmemb;
	int fd = -1;
	if (stream == stdout || stream == stderr)
		fd = XENEVA_STDOUT;
	else if (stream == stdin)
		fd = XENEVA_STDIN;
	else
		fd = stream->_file_num;
	size_t ret_bytes = _KeReadFile(fd, ptr, _length);
	return ret_bytes;
}

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
size_t fwrite(void* ptr, size_t sz, size_t nmemb, FILE* stream) {
	if (!stream)
		return 0;
	char* aligned_ = (char*)ptr;
	size_t ret_bytes = 0;
	if (stream == stdout) {
		ret_bytes = _KeWriteFile(XENEVA_STDOUT, aligned_, sz * nmemb);
	} else if (stream == stderr) {
		ret_bytes = _KeWriteFile(XENEVA_STDERR, aligned_, sz * nmemb);
	} else if (stream == stdin) {
		ret_bytes = _KeWriteFile(XENEVA_STDIN, aligned_, sz * nmemb);
	} else {
		if (stream->_file_num == -1)
			return 0;
		size_t ret_bytes = _KeWriteFile(stream->_file_num, aligned_, sz * nmemb);
	}
	return ret_bytes;
}

int fputc(int c, FILE* stream) {
	char data[] = {c};
	int fd = -1;
	if (stream == stdout || stream == stderr)
		fd = XENEVA_STDOUT;
	else if (stream == stdin)
		fd = XENEVA_STDIN;
	else
		fd = stream->_file_num;
	size_t ret_bytes = _KeWriteFile(fd, data, 1);
	return c;
}

int fputs(const char* s, FILE* stream) {
	while (*s) {
		fputc(*s++, stream);
	}
	return 0;
}

/*
 * ftell -- returns the current file position of the
 * specified stream with respect to the starting of
 * the file
 * @param fp -- pointer to FILE structure
 */
long ftell(FILE* fp) {
	return fp->curr_pos;
}

/*
 * fseek -- moves or changes the position of
 * the file pointer which is being used to 
 * read the file, to a specified byte
 * offset position
 * @param fp -- pointer to FILE structure
 * @param offset -- offset in bytes
 * @param pos -- position mode
 */
int fseek(FILE* fp, long int offset, int pos) {
	int val = 0;
	int newPos = 0;
	switch (pos) {
	case SEEK_SET:
		fp->pos = fp->base + offset;
		newPos = offset;
		fp->curr_pos = newPos;
		break;
	case SEEK_CUR:
		fp->pos = (fp->pos + offset);
		newPos = (fp->curr_pos + offset);
		fp->curr_pos = newPos;
		break;
	case SEEK_END:
		fp->pos = (fp->base + fp->size);
		newPos = fp->size;
		fp->curr_pos = newPos;
		break;
	}
	_KeFileSetOffset(fp->_file_num, newPos);
	return val;
}

/*
 * fgetc -- reads a single character from the
 * input stream at the current position
 * and increases the file pointer.
 * @param fp -- pointer to FILE structure
 */
int fgetc(FILE* fp) {
	int len = ftell(fp);
	if (len == fp->size) {
		fp->eof = 1;
		return 0;
	}
	return *(fp->pos++);
}

/*
 * fclose -- closes a file
 * @param fp -- pointer to FILE structure
 */
int fclose(FILE* fp) {
	if (!fp)
		return -1;
	if (fp->_file_num == -1) {
		free(fp);
		return -1;
	}

	int ret = _KeCloseFile(fp->_file_num);
	free(fp);

	return ret;
}

int putchar(int c) {
	return fputc(c, stdout);
}

int puts(const char* s) {
	fwrite((void*)s, 1, strlen(s), stdout);
	fwrite((void*)"\n", 1, 1, stdout);
	return 0;
}

int remove(const char* pathname) {
	return _KeRemoveFile((char*)pathname);
}

int rename(const char* oldpath, const char* newpath) {
	return -1;
}

int sprintf(char* output, const char* format, ...) {
	va_list list;
	va_start(list, format);
	int len = 0;
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);
	va_end(list);
	return len;
}

int snprintf(char* output, size_t sz, const char* format, ...) {
	va_list list;
	va_start(list, format);
	int len = 0;
	memset(output, 0, sz);
	len = _xeprint(output, sz, format, list);
	va_end(list);
	return len;
}

static void __stdout_flush_raw() {
	if (output_printf_len > 0) {
		_KeWriteFile(XENEVA_STDOUT, output_printf, output_printf_len);
		output_printf_len = 0;
	}
}

static void __stdout_putc(char c) {
	output_printf[output_printf_len++] = c;
	if (c == '\n' || output_printf_len >= 512) {
		_KeWriteFile(XENEVA_STDOUT, output_printf, output_printf_len);
		output_printf_len = 0;
	}
}

static void __stdout_write(const char* data, size_t len) {
	for (size_t i = 0; i < len; i++)
		__stdout_putc(data[i]);
}

int printf(const char* format, ...) {
	va_list list;
	va_start(list, format);
	char _output[MAX_STRING_LENGTH + 1];
	memset(_output, '\0', MAX_STRING_LENGTH);
	int len = _xeprint(_output, MAX_STRING_LENGTH, format, list);
	va_end(list);
	//if (strchr(_output, '\n') == 0)
	//_KeWriteFile(XENEVA_STDOUT, _output, strlen(_output));
	__stdout_write(_output, len);
	return len;
}

int fflush(FILE* stream) {
	//_KeWriteFile(stream->_file_num,stream->ptr, )
	//not implemented
	if (stream != stdout)
		return 1;
	__stdout_flush_raw();
	return 0;
}

int vfprintf(FILE* stream, const char* format, va_list list) {
	int status = 0;
	int len = 0;
	char output[MAX_STRING_LENGTH + 1];
	if ((stream == stdout) || (stream == stderr)) {
		status = vprintf(format, list);
		return status;
	}
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);
	va_end(list);
	if (len > 0) {
		_KeWriteFile(XENEVA_STDIN, output, strlen(output) - 1);
	}
}

int vsnprintf(char* output, size_t sz, const char* format, va_list ap) {
	int len = 0;
	sz = min(sz, MAX_STRING_LENGTH);

	memset(output, 0, sz);
	len = _xeprint(output, sz, format, ap);
	return len;
}

int vsprintf(char* output, const char* format, va_list list) {
	int len = 0;
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);

	if (len < 0)
		return 0;
	else
		return len;
}

int vprintf(const char* format, va_list list) {
	int len = 0;
	char output[MAX_STRING_LENGTH + 1];
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);
	int size_written = 0;
	if (len > 0)
		size_written = _KeWriteFile(XENEVA_STDOUT, output, strlen(output) - 1);
	return size_written;
}

int fprintf(FILE* stream, const char* format, ...) {
	int status = 0;
	va_list list;
	int len = 0;
	char output[MAX_STRING_LENGTH + 1];
	va_start(list, format);
	if ((stream == stdout) || (stream == stderr)) {
		status = vprintf(format, list);
		va_end(list);
		return status;
	}
	len = _xeprint(output, MAX_STRING_LENGTH, format, list);
	va_end(list);
	if (len > 0) {
		_KeWriteFile(stream->_file_num, output, strlen(output) - 1);
	}
	return len;
}

/* getchar -- read a single character
 * from stdin
 */
int getchar() {
	char buf[1];
	memset(buf, 0, 1);
	int read_bytes = _KeReadFile(XENEVA_STDIN, buf, 1);
	return buf[0];
}

int scanf(const char* format, ...) {
	// not implemented
	return 0;
}

int sscanf(const char* str, const char* format, ...) {
	return 0;
}