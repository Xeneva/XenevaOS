/**
* BSD 2-Clause License
*
* Copyright (c) 2026, Manas Kamal Choudhury
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

/*
 * mbedtls_hardware_poll for MBEDTLS_ENTROPY_HARDWARE_ALT.
 * Guest: mainline virtio-rng.ko via DCL at /dev/hwrng.
 * Host self-test: /dev/urandom.
 */

#include "mbedtls/build_info.h"

#if defined(MBEDTLS_ENTROPY_C) && defined(MBEDTLS_ENTROPY_HARDWARE_ALT)

#include <stddef.h>
#include "mbedtls/entropy.h"

#if defined(__linux__)
#include <stdio.h>

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen) {
	FILE* f;
	size_t n;
	(void)data;
	*olen = 0;
	f = fopen("/dev/urandom", "rb");
	if (!f)
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	n = fread(output, 1, len, f);
	fclose(f);
	if (n != len)
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	*olen = n;
	return 0;
}

#else
#include <sys/_kefile.h>
#include <sys/_keproc.h>

static int read_entropy_file(const char* path, unsigned char* output, size_t len) {
	int fd;
	size_t got = 0;
	int spins = 0;

	fd = _KeOpenFile((char*)path, FILE_OPEN_READ_ONLY);
	if (fd < 0)
		return -1;
	/* The first virtio-rng buffer is still in flight at boot. A read
	 * then returns 0; the same read after the used ring lands succeeds.
	 * Wait for that buffer instead of failing the first TLS handshake. */
	while (got < len && spins < 40) {
		size_t room = len - got;
		size_t n = _KeReadFile(fd, output + got, room);
		/* A failed read comes back as a negative int in a size_t. */
		if (n == 0 || n > room) {
			spins++;
			_KeProcessSleep(25);
			continue;
		}
		got += n;
		spins = 0;
	}
	_KeCloseFile(fd);
	return got == len ? 0 : -1;
}

int mbedtls_hardware_poll(void* data, unsigned char* output, size_t len, size_t* olen) {
	(void)data;
	*olen = 0;
	/* /dev/urandom is the normal source; /dev/hwrng stays as the fallback
	 * for images where the random device was not created. */
	if (read_entropy_file("/dev/urandom", output, len) != 0 &&
		read_entropy_file("/dev/hwrng", output, len) != 0)
		return MBEDTLS_ERR_ENTROPY_SOURCE_FAILED;
	*olen = len;
	return 0;
}
#endif

#endif
