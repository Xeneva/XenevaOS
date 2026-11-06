/**
* @file _gettime.cpp
* 
* BSD 2-Clause License
*
* Copyright (c) 2022, Manas Kamal Choudhury
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
#include <sys/socket.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/netdb.h>
#include <sys/_ketime.h>
#include <time.h>
#include <unistd.h>

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
	uint32_t referenece_ts_frac;

	uint32_t origin_ts_sec;
	uint32_t origin_ts_frac;

	uint32_t receive_ts_sec;
	uint32_t receive_ts_frac;

	uint32_t transmit_ts_sec;
	uint32_t transmit_ts_frac;
} ntp_packet_t;
#pragma pack(pop)

#define NTP_PORT		123
#define NTP_PACKET_SIZE 48

#define NTP_UNIX_EPOCH_DELTA 2208988800UL

static uint32_t swap32(uint32_t v) {
	return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) | ((v & 0x00FF0000u) >> 8) |
		   ((v & 0xFF000000u) >> 24);
}

static int64_t floordiv(int64_t a, int64_t b) {
	int64_t q = a / b;
	int64_t r = a % b;
	if ((r != 0) && ((r < 0) != (b < 0)))
		q--;
	return q;
}

static int64_t floormod(int64_t a, int64_t b) {
	int64_t r = a % b;
	if (r != 0 && (r < 0) != (b < 0))
		r += b;
	return r;
}

/**
 * AI Generated code
 */
static void civil_from_days(int64_t z, int* y, unsigned* m, unsigned* d) {
	z += 719468;
	int64_t era = (z >= 0 ? z : z - 146096) / 146097;
	unsigned doe = (unsigned)(z - era * 146097);
	unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	int64_t y0 = (int64_t)yoe + era * 400;
	unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	unsigned mp = (5 * doy + 2) / 153;
	*d = doy - (153 * mp + 2) / 5 + 1;
	*m = mp + (mp < 10 ? 3 : -9);
	*y = (int)(y0 + (*m <= 2));
}

static void print_human_time(uint32_t unix_sec, uint32_t nsec, int32_t offset_seconds) {
	int64_t days = (int64_t)unix_sec / 86400;
	uint32_t secs_of_day = unix_sec % 86400;

	int y;
	unsigned mo, d;
	civil_from_days(days, &y, &mo, &d);

	unsigned hh = secs_of_day / 3600;
	unsigned mm = (secs_of_day % 3600) / 60;
	unsigned ss = secs_of_day % 60;

	int32_t off_h = offset_seconds / 3600;
	int32_t off_m = (offset_seconds < 0 ? -offset_seconds : offset_seconds) % 3600 / 60;
	_KePrint("Current Celender : %d :: %d :: %d \r\n", mo, y, d);
}

static void print_ist_time(int64_t utc_sec) {
	int64_t local_sec = utc_sec + TZ_SEC_IST_INDIA;
	int64_t secs_of_day = local_sec % 86400;
	if (secs_of_day < 0)
		secs_of_day += 86400;

	unsigned hh = (unsigned)(secs_of_day / 3600);
	unsigned mm = (unsigned)((secs_of_day % 3600) / 60);
	unsigned ss = (unsigned)(secs_of_day % 60);

	_KePrint("IST Time = %d :: %d :: %d \r\n", hh, mm, ss);
}

static void ntp_build_request(ntp_packet_t* pack) {
	memset(pack, 0, sizeof(ntp_packet_t));
	pack->li = 0;
	pack->vn = 4;
	pack->mode = 3;
}

static void
ntp_ts_to_unix(uint32_t sec_be, uint32_t frac_be, uint32_t* unix_sec, uint32_t* frac_out) {
	uint32_t sec = swap32(sec_be);
	uint32_t frac = swap32(frac_be);
	*unix_sec = sec - (uint32_t)NTP_UNIX_EPOCH_DELTA;
	*frac_out = frac;
}

static void ntp_ts_to_tmespec(uint32_t sec_be, uint32_t frac_be, timespec* ts) {
	uint32_t sec = swap32(sec_be);
	uint32_t frac = swap32(frac_be);

	ts->tv_sec = (time_t)(sec - NTP_UNIX_EPOCH_DELTA);
	ts->tv_nsec = (long)(((uint64_t)frac * 1000000000ULL) >> 32);
}

int ntp_get_time(const char* server_ip, uint32_t* out_unix_sec, uint32_t* out_unix_ns) {
	ntp_packet_t req, resp;
	int sock, ret;

	ntp_build_request(&req);

	struct hostent* he = gethostbyname(server_ip);
	if (he == NULL || he->h_addr_list[0] == NULL)
		return -5;

	struct in_addr server_addr;
	memcpy(&server_addr, he->h_addr_list[0], sizeof(in_addr));

	sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock < 0)
		return -1;

	sockaddr_in dest;
	dest.sin_family = AF_INET;
	dest.sin_port = htons(NTP_PORT);
	dest.sin_addr = server_addr;

	for (int i = 0; i < 4; i++) {
		ret = sendto(sock, &req, sizeof(req), 0, (sockaddr*)&dest, sizeof(sockaddr_in));
		sockaddr_in from;
		socklen_t fromLen = sizeof(sockaddr_in);
		ret = recvfrom(sock, &resp, sizeof(ntp_packet_t), 0, (sockaddr*)&from, &fromLen);

		if (resp.mode != 4 || resp.stratum == 0) {
			_KePrint("kiss-o'-death \r\n");
		}

		/*	if (resp.mode == 4 || resp.stratum != 0)
			break;*/

		sleep(1);
	}
	ntp_ts_to_unix(resp.transmit_ts_sec, resp.transmit_ts_frac, out_unix_sec, out_unix_ns);
	timespec ss;

	_KePrint("Raw UTC: Transmit_ts_sec : %d, ts_frac : %d \r\n",
			 resp.transmit_ts_sec,
			 resp.transmit_ts_frac);

	ntp_ts_to_tmespec(resp.transmit_ts_sec, resp.transmit_ts_frac, &ss);

	_KePrint("Timspec sec : %d, nsec :%d \r\n", ss.tv_sec, ss.tv_nsec);

	/* set kernel walltime value, but be sure you own the
	 * credentials and caps to be able to modify walltime
	 */
	_KeSetWalltime(ss.tv_sec, ss.tv_nsec);

	return 0;
}

void NamdaphaGetOnlineTime() {
	const char* server = "pool.ntp.org";
	uint32_t sec, ns;

	//int ret = ntp_get_time(server, &sec, &ns);

	int64_t sec_, nsec_;
	_KeGetWalltime(&sec_, &nsec_);
	print_ist_time(sec_);
	print_human_time(sec_, nsec_, TZ_SEC_IST_INDIA);
	_KePrint("unix time: %d sec, %d ns fraction \r\n", sec_, nsec_);
}

/**
 * @brief NamdaphaGetYOD -- get year, month and date
 * @param year -- memory location where to store year
 * @param month -- memory location where to store month
 * @param day -- memory location where to store day
 * @param tz_offset -- timezone offset in second
 */
void NamdaphaGetYMD(uint32_t* year, uint32_t* month, uint32_t* day, int32_t tz_offset) {
	int64_t sec_, nsec_;
	_KeGetWalltime(&sec_, &nsec_);
	/* the wall clock is UTC (NamdaphaGetWallTime adds the zone too): without
	 * this the date lagged a day from 00:00 to 05:30 IST */
	sec_ += tz_offset;
	if (sec_ < 0)
		sec_ = 0;

	int64_t days = (int64_t)sec_ / 86400;
	uint32_t secs_of_day = sec_ % 86400;

	int y;
	unsigned mo, d;
	civil_from_days(days, &y, &mo, &d);
	*year = y;
	*month = mo;
	*day = d;
}

/**
 * NamdaphaGetWallTime -- get the walltime update
 * @param hour -- memory location to store hour
 * @param minute -- memory location to store minute
 * @param sec -- memory location to store second
 * @param tz_offset -- timezone offset
 */
void NamdaphaGetWallTime(int* hour, int* minute, int* sec, int tz_offset) {
	int64_t sec_, nsec_;
	_KeGetWalltime(&sec_, &nsec_);

	int64_t local_sec = sec_ + tz_offset;
	int64_t secs_of_day = local_sec % 86400;
	if (secs_of_day < 0)
		secs_of_day += 86400;

	unsigned hh = (unsigned)(secs_of_day / 3600);
	unsigned mm = (unsigned)((secs_of_day % 3600) / 60);
	unsigned ss = (unsigned)(secs_of_day % 60);
	*hour = hh;
	*minute = mm;
	*sec = ss;
}