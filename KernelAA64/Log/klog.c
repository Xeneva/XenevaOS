/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2026, Manas Kamal Choudhury
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

#include <Log/klog.h>
#include <stdint.h>
#include <string.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <timer.h>
#include <Log/_print.h>
#include <aucon.h>
#include <Drivers/uart.h>
#include <stdbool.h>

#define LOG_BUFFER_SIZE (128 * 1024)
#define LOG_LINE_MAX	256

typedef struct _blog_ring_ {
	char buffer[LOG_BUFFER_SIZE];
	uint32_t head;
	uint32_t tail;
	uint32_t len;
	uint64_t seq;
	//lock
} BLogRing;

static bool _Blog_use_uart;

/** level tags */
static const char* level_tag[BORDOISILA_DEBUG_LEVEL_COUNT] = {
	"EMERG", "ERROR", "WARN", "INFO", "DEBUG"};

static const uint32_t level_col[BORDOISILA_DEBUG_LEVEL_COUNT] = {
	0xFFFF3B30, 0xFFD9534F, 0xFFFFC107, 0xFFE6E6E6, 0xFF888888};

static BLogRing _log_ring;
BLogLevel _log_console_level = BORDOISILA_DEBUG;

/**
 * @brief B_KLogInit -- initialize bordoisila
 * logging system
 */
void B_KLogInit() {
	_log_ring.head = 0;
	_log_ring.tail = 0;
	_log_ring.len = 0;
	_log_ring.seq = 0;
	_Blog_use_uart = 0;
}

static void _BLog_Write_Locked(const char* data, uint32_t n) {
	if (n >= LOG_BUFFER_SIZE) {
		data += (n - (LOG_BUFFER_SIZE - 1));
		n = LOG_BUFFER_SIZE - 1;
	}

	for (uint32_t i = 0; i < n; i++) {
		_log_ring.buffer[_log_ring.head] = data[i];
		_log_ring.head = (_log_ring.head + 1) % LOG_BUFFER_SIZE;

		if (_log_ring.len < LOG_BUFFER_SIZE) {
			_log_ring.len++;
		} else {
			_log_ring.tail = (_log_ring.tail + 1) % LOG_BUFFER_SIZE;
		}
	}

	_log_ring.seq++;
}

static void _BlogRingWrite(const char* data, uint32_t n) {
	//spinlock
	_BLog_Write_Locked(data, n);
	//spin unlock
}

void BlogDump(BLogSinkFunc sink, void* ctx) {
	if (!sink)
		return;

	//lock

	uint32_t pos = _log_ring.tail;
	uint32_t left = _log_ring.len;

	char snapshot[512];
	//unlock

	while (left > 0) {
		uint32_t chunk = left < sizeof(snapshot) ? left : sizeof(snapshot);

		//lock
		for (uint32_t i = 0; i < chunk; i++) {
			snapshot[i] = _log_ring.buffer[(pos + i) % LOG_BUFFER_SIZE];
		}

		//unlock

		sink(snapshot, chunk, ctx);

		pos = (pos + chunk) % LOG_BUFFER_SIZE;

		left -= chunk;
	}
}

static inline void _blog_get_uptime(uint64_t* sec, uint32_t* usec) {
	uint64_t cntpct = get_cntpct_el0();
	uint64_t cntfrq = get_cntfrq_el0();

	if (cntfrq == 0) {
		*sec = 0;
		*usec = 0;
		return;
	}

	*sec = cntpct / cntfrq;
	*usec = (uint32_t)(((cntpct % cntfrq) * 1000000ULL) / cntfrq);
}

extern void store_x2_x7(uint8_t* buffer);

/**
 * @brief BPrintK -- print formatted string to circular buffer
 * @param level -- logging level
 * @param fmt -- formatted string
 */
void BPrintK(BLogLevel level, const char* fmt, ...) {
	uint8_t buffer[192];
	store_x2_x7(buffer);

	char line[LOG_LINE_MAX];

	uint64_t sec;
	uint32_t usec;

	if (level >= BORDOISILA_DEBUG_LEVEL_COUNT)
		level = BORDOISILA_DEBUG_LEVEL_COUNT;

	_blog_get_uptime(&sec, &usec);

	int prefix_len = _snprintf(
		line, sizeof(line), "[%d.%d] [%s] ", (unsigned long long)sec, usec, level_tag[level]);

	if (prefix_len < 0)
		prefix_len = 0;

	if ((uint32_t)prefix_len >= sizeof(line))
		prefix_len = sizeof(line) - 1;

	va_list args = (va_list)buffer;
	int body_len = _vsnprintf(line + prefix_len, sizeof(line) - prefix_len, fmt, args);
	va_end(args);

	if (body_len < 0)
		body_len = 0;
	int total = prefix_len + body_len;
	if (total >= (int)sizeof(line)) {
		total = sizeof(line) - 1;
	}

	if (total == 0 || line[total - 1] != '\n') {
		line[total++] = '\n';
	}

	_BlogRingWrite(line, (uint32_t)total);

	if (!_Blog_use_uart)
		AuPutS_Color(line, level_col[level]);
	else
		uartPuts(line);
}

/**
 * @brief _BlogBypassAuConsole -- if logging was using 
 * framebuffer, bypass it to uart
 */
void _BlogBypassAuConsole() {
	if (_Blog_use_uart)
		return;
	else
		_Blog_use_uart = 1;
}