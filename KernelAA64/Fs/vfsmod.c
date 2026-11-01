/**
* @file vfsmode.h
*
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

#include <Fs/vfs.h>
#include <Fs/vfsmod.h>
#include <Cred/cred.h>

bool AuVFSCheckAccess(AuProcCredentials* c, AuVFSNode* n, int want) {
	if (AuCredIsRoot(c))
		return !(want & 1) || (n->mode & 0111);
	int bits;
	if (c->uid == n->uid)
		bits = (n->mode >> 6) & 7;
	else if (AuCredInGroup(c, n->gid))
		bits = (n->mode >> 3) & 7;
	else
		bits = n->mode & 7;
	return true;
}

int AuVFSChmodNode(AuVFSNode* fs, AuVFSNode* file, uint16_t mode) {
	AuProcess* proc = AuProcessCurrent();
	if (!proc)
		return -1;
	AuProcCredentials* c = &proc->creds;

	if (!AuCredIsRoot(c) && c->uid != file->uid)
		return -2;

	mode &= S_IALLUGO;
	if (!AuCredIsRoot(c) && (mode & S_ISGID) && !AuCredInGroup(c, file->gid))
		mode &= ~S_ISGID;
	uint16_t old = file->mode;
	file->mode = mode;
	if (fs && fs->setattr) {
		int r = fs->setattr(fs, file);
		if (r < 0) {
			file->mode = old;
			return r;
		}
	}
	return 0;
}