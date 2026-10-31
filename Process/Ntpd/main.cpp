/**
* BSD 2-Clause License
*
* Copyright (c) 2026, Xeneva Contributors
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

/**
 * ntpd -- minimal background SNTPv4 client. Syncs the kernel walltime
 * (_KeSetWalltime) on boot and periodically thereafter, since the PL031
 * RTC is never enabled and walltime otherwise sits at unix epoch 0.
 */

#include <stdint.h>
#include <_xeneva.h>
#include <stdio.h>
#include <sys/_keproc.h>
#include <sys/_kefile.h>
#include <sys/socket.h>
#include <sys/netdb.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/_ketime.h>
#include <time.h>
#include <unistd.h>
#include <stdlib.h>

#pragma pack(push, 1)
typedef struct {
	uint8_t mode : 3;
	uint8_t vn : 3;
	uint8_t li : 2;
	uint8_t stratum;
	uint8_t poll;
	int8_t precision;
	uint32_t root_delay;
	uint32_t root_dispersion;
	uint32_t reference_id;

	uint32_t reference_ts_sec;
	uint32_t reference_ts_frac;

	uint32_t origin_ts_sec;
	uint32_t origin_ts_frac;

	uint32_t receive_ts_sec;
	uint32_t receive_ts_frac;

	uint32_t transmit_ts_sec;
	uint32_t transmit_ts_frac;
} ntp_packet_t;
#pragma pack(pop)

#define NTP_PORT 123
#define NTP_UNIX_EPOCH_DELTA 2208988800UL

/* how many pool.ntp.org tries per sync attempt aren't needed -- one
 * request, polled for a reply -- see ntp_sync_once() below */
#define NTP_POLL_TRIES 20
#define NTP_POLL_SLEEP_MS 150

/* on success, wait this long before the next sync; on failure, retry
 * sooner but never busy-loop --axiss */
#define NTP_SYNC_INTERVAL_SEC 1800
#define NTP_RETRY_BACKOFF_SEC 30

static const char* NTP_SERVER = "pool.ntp.org";

static uint32_t swap32(uint32_t v) {
	return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) |
		   ((v & 0xFF000000u) >> 24);
}

static void ntp_build_request(ntp_packet_t* pack) {
	memset(pack, 0, sizeof(ntp_packet_t));
	pack->li = 0;
	pack->vn = 4;
	pack->mode = 3; /* client */
}

static void ntp_ts_to_timespec(uint32_t sec_be, uint32_t frac_be, timespec* ts) {
	uint32_t sec = swap32(sec_be);
	uint32_t frac = swap32(frac_be);
	ts->tv_sec = (time_t)(sec - NTP_UNIX_EPOCH_DELTA);
	ts->tv_nsec = (long)(((uint64_t)frac * 1000000000ULL) >> 32);
}

/**
 * @brief ntp_sync_once -- resolve NTP_SERVER, send one SNTP request,
 * poll for a valid reply, and apply it via _KeSetWalltime. recvfrom()
 * returns immediately if no packet is queued yet (no kernel-side
 * blocking/timeout support), so this polls in a bounded loop rather
 * than trusting a single recvfrom() call right after sendto() --axiss
 * @return true if the walltime was actually updated, false otherwise
 */
static bool ntp_sync_once() {
	hostent* he = gethostbyname(NTP_SERVER);
	if (!he || !he->h_addr_list[0]) {
		_KePrint("ntpd: failed to resolve %s \n", NTP_SERVER);
		return false;
	}

	int sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0) {
		_KePrint("ntpd: failed to create socket \n");
		return false;
	}

	sockaddr_in dest;
	memset(&dest, 0, sizeof(dest));
	dest.sin_family = AF_INET;
	dest.sin_port = htons(NTP_PORT);
	memcpy(&dest.sin_addr, he->h_addr_list[0], sizeof(in_addr));

	ntp_packet_t req;
	ntp_build_request(&req);

	if (sendto(sock, &req, sizeof(req), 0, (sockaddr*)&dest, sizeof(dest)) < 0) {
		_KePrint("ntpd: sendto failed \n");
		_KeCloseFile(sock);
		return false;
	}

	ntp_packet_t resp;
	memset(&resp, 0, sizeof(resp));
	bool got_reply = false;

	for (int i = 0; i < NTP_POLL_TRIES; i++) {
		sockaddr_in from;
		socklen_t fromLen = sizeof(from);
		int ret = recvfrom(sock, &resp, sizeof(resp), 0, (sockaddr*)&from, &fromLen);
		if (ret > 0 && resp.mode == 4 && resp.stratum != 0) {
			got_reply = true;
			break;
		}
		_KeProcessSleep(NTP_POLL_SLEEP_MS);
	}

	_KeCloseFile(sock);

	if (!got_reply) {
		_KePrint("ntpd: no valid reply from %s \n", NTP_SERVER);
		return false;
	}

	timespec ts;
	ntp_ts_to_timespec(resp.transmit_ts_sec, resp.transmit_ts_frac, &ts);
	_KeSetWalltime(ts.tv_sec, ts.tv_nsec);
	_KePrint("ntpd: synced walltime to %d sec (unix) \n", (int)ts.tv_sec);
	return true;
}

/*
 * main -- sync on boot, then keep re-syncing periodically. Never sets
 * the walltime from a failed/partial exchange, never busy-loops on
 * failure --axiss
 */
int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;

	_KePrint("ntpd: starting, target=%s \n", NTP_SERVER);

	while (1) {
		bool ok = ntp_sync_once();
		if (ok) {
			sleep(NTP_SYNC_INTERVAL_SEC);
		} else {
			sleep(NTP_RETRY_BACKOFF_SEC);
		}
	}

	return 0;
}
