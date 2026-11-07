/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2024, Manas Kamal Choudhury
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
#include <stdlib.h>

/*
* main -- main entry
*/
int main(int argc, char* argv[]) {
	addrinfo hints;
	addrinfo* res = NULL;
	addrinfo* rp;
	int err;
	char buf[64];
	uint32_t server;
	int n = 0;
	const char* name = NULL;

	for (int i = 0; i < argc; i++) {
		if (!argv[i] || argv[i][0] == '\0')
			continue;
		if (argv[i][0] == '/' || strstr(argv[i], ".exe") || strcmp(argv[i], "nslook") == 0)
			continue;
		if (argv[i][0] == '-') {
			printf("nslook: unknown option %s\n", argv[i]);
			_KePrint("nslook: unknown option %s\r\n", argv[i]);
			return 1;
		}
		name = argv[i];
		break;
	}

	if (!name) {
		printf("usage: nslook <name>\n");
		_KePrint("nslook: usage: nslook <name>\r\n");
		return 1;
	}

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	err = getaddrinfo(name, NULL, &hints, &res);
	if (err != 0) {
		printf("nslook: %s: %s\n", name, gai_strerror(err));
		_KePrint("nslook: %s: %s\r\n", name, gai_strerror(err));
		return 1;
	}

	server = xe_dns_last_server();
	printf("Non-authoritative answer\n");
	printf("Name: %s\n", name);
	_KePrint("nslook: Name: %s\r\n", name);
	if (server) {
		struct in_addr ina;
		ina.s_addr = server;
		printf("Server: %s\n", inet_ntoa(ina));
		_KePrint("nslook: Server: %s\r\n", inet_ntoa(ina));
	} else {
		printf("Server: (local zone / cache)\n");
		_KePrint("nslook: Server: (local zone / cache)\r\n");
	}

	for (rp = res; rp; rp = (addrinfo*)rp->ai_next) {
		const char* s = NULL;
		if (rp->ai_family == AF_INET) {
			sockaddr_in* in = (sockaddr_in*)rp->ai_addr;
			s = inet_ntop(AF_INET, &in->sin_addr, buf, sizeof(buf));
		} else if (rp->ai_family == AF_INET6) {
			sockaddr_in6* in6 = (sockaddr_in6*)rp->ai_addr;
			s = inet_ntop(AF_INET6, &in6->sin6_addr, buf, sizeof(buf));
		}
		if (s) {
			printf("Address: %s\n", s);
			_KePrint("nslook: Address: %s\r\n", s);
			n++;
		}
	}

	freeaddrinfo(res);
	return n > 0 ? 0 : 1;
}
