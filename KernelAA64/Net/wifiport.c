/**
 * BSD 2-Clause License
 *
 * Copyright (c) 2022-2026, Manas Kamal Choudhury
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
#include <aurora.h>
#include <Net/wifi.h>

static void (*wifi_tx)(void* frame, uint16_t len);
static int (*wifi_rx)(void* frame, int len);

/* The kernel link writes KernelAA64.lib from dllexport symbols. */
AU_EXPORT void AuWifiPortalSetTx(void (*fn)(void* frame, uint16_t len)) {
	wifi_tx = fn;
}

AU_EXPORT void AuWifiPortalTx(void* frame, uint16_t len) {
	if (wifi_tx && frame && len)
		wifi_tx(frame, len);
}

AU_EXPORT void AuWifiPortalSetRx(int (*fn)(void* frame, int len)) {
	wifi_rx = fn;
}

AU_EXPORT int AuWifiPortalRx(void* frame, int len) {
	if (!wifi_rx || !frame || len <= 0)
		return 0;
	return wifi_rx(frame, len);
}
