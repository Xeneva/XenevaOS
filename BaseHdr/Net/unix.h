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

#ifndef __NET_UNIX_H__
#define __NET_UNIX_H__

#include <Net/socket.h>

/*
 * AuUnixCreateSocket -- create an AF_UNIX stream or datagram socket.
 * Stream sockets are connected in-memory to a server-side socket queued on
 * the listener; datagram sockets exchange framed messages through the same
 * registry. Paths are namespace entries, not filesystem nodes.
 */
extern int AuUnixCreateSocket(int type, int protocol);

/*
 * AuUnixCreatePair -- create a connected AF_UNIX socket pair.
 * Supports SOCK_STREAM and SOCK_DGRAM only. On success sv[0] and sv[1]
 * are installed in the current process and 0 is returned; on failure a
 * negative AU_ERR_* value is returned and nothing is installed.
 */
extern int AuUnixCreatePair(int type, int protocol, int sv[2]);

/* True when the socket family/type is a UNIX-domain socket. */
extern int AuUnixIsSocket(AuSocket* sock);

#endif
