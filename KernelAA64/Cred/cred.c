/**
* @file cred.c
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

#include <Cred/group.h>
#include <Cred/user.h>
#include <Cred/cred.h>
#include <string.h>
#include <process.h>
#include <Fs/vfs.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <aucon.h>

/** global group ID's for kernel use **/
static GID_NUM _groups[AURORA_MAX_GROUPS];

static inline uint64_t _mix64(uint64_t x) {
	x ^= (x >> 30);
	x *= 0xbf58476d1ce4e5b9ULL;
	x ^= (x >> 27);
	x *= 0x94d049bb133111ebULL;
	x ^= (x >> 31);
	return x;
}

/**
 * @brief _cred_gen_gid -- generate a gid 
 * @param min -- min number
 * @param max -- max number
 * @param seed -- seed value
 */
static uint32_t _cred_gen_gid(uint32_t min, uint32_t max, uint64_t* seed) {
	uint64_t tick = get_cntvct_el0();
	*seed ^= tick;
	*seed = _mix64(*seed);

	uint32_t range = max - min + 1;
	return min + (uint32_t)(*seed % range);
}

/** @brief _collision_check_gid -- check if already a gid number is availble
 * in the table
 */
static int _collision_check_gid(int count, GID_NUM val) {
	for (int i = 0; i < count; i++)
		if (_groups[i] == val)
			return 1;
	return 0;
}

/**
 * @brief AuCredGroupInitialize -- initialize default global group
 * id's
 */
void AuCredGroupInitialize() {
	for (int i = 0; i < AURORA_MAX_GROUPS; i++) {
		_groups[i] = 0;
	}

	uint64_t seed = get_cntvct_el0();
	for (int i = 0; i < 100; i++)
		_wfi();
	seed ^= get_cntvct_el0() << 17;
	for (int i = 0; i < 200; i++)
		_wfi();
	seed ^= get_cntvct_el0() << 37;
	seed = _mix64(seed);

	int filled = 0;

#define GID_GROUP_NET_MIN 0x1000
#define GID_GROUP_NET_MAX 0x1FFF

	UARTDebugOut("[aurora]: generating global group table... \r\n");
	for (int i = 0; i < GID_GLOBAL_NET_COUNT; i++) {
		GID_NUM g;
		do {
			g = (GID_NUM)_cred_gen_gid(GID_GROUP_NET_MIN, GID_GROUP_NET_MAX, &seed);
		} while (_collision_check_gid(filled, g));
		_groups[GID_GLOBAL_NET + i] = g;
		filled++;
	}

	/** fill up the IPC group gid **/
#define GID_GROUP_IPC_MIN 0x2000
#define GID_GROUP_IPC_MAX 0x2FFF

	for (int i = 0; i < GID_GLOBAL_IPC_COUNT; i++) {
		GID_NUM g;
		do {
			g = (GID_NUM)_cred_gen_gid(GID_GROUP_IPC_MIN, GID_GROUP_IPC_MAX, &seed);
		} while (_collision_check_gid(filled, g));
		_groups[GID_GLOBAL_IPC + i] = g;
		filled++;
	}

	/** fill up the MISC group gid **/
#define GID_GROUP_MISC_MIN 0x3000
#define GID_GROUP_MISC_MAX 0x3FFF
	for (int i = 0; i < GID_GLOBAL_MISC_COUNT; i++) {
		GID_NUM g;
		do {
			g = (GID_NUM)_cred_gen_gid(GID_GROUP_MISC_MIN, GID_GROUP_MISC_MAX, &seed);
		} while (_collision_check_gid(filled, g));
		_groups[GID_GLOBAL_MISC + i] = g;
		filled++;
	}
	UARTDebugOut("[aurora]: global group table initialized \r\n");
}

/**
 * AuCredGroupAdd -- add group value to a category
 * @param category -- category number
 * @param value -- value to add
 */
int AuCredGroupAdd(uint8_t category, GID_NUM value) {
	if (category > AURORA_MAX_GROUPS)
		return -1;

	if (_groups[category] == 0) {
		_groups[category] = value;
		return 0;
	}
	/** could not add value **/
	return 1;
}

/**
 * @brief AuCredGetGroupCategory -- get a group category 
 * number by looking it name
 * @param string -- name of the group
 */
uint8_t AuCredGetGroupCategory(const char* string) {
	if (strlen(string) == 0)
		return AURORA_MAX_GROUPS + 1;

	/*if (strcmp(string, GROUP_NAME_NETWORK) == 0)
		return AURORA_GID_NETWORK;
	else if (strcmp(string, GROUP_NAME_AUDIO) == 0)
		return AURORA_GID_AUDIO;
	else if (strcmp(string, GROUP_NAME_TTY) == 0)
		return AURORA_GID_TTY;
	else if (strcmp(string, GROUP_NAME_STORAGE) == 0)
		return AURORA_GID_STORAGE;
	else if (strcmp(string, GROUP_NAME_GRAPHICS) == 0)
		return AURORA_GID_GRAPHICS;
	else if (strcmp(string, GROUP_NAME_IPC_POSTBOX) == 0)
		return AURORA_GID_IPC_POSTBOX;*/

	return AURORA_MAX_GROUPS + 1;
}

/**
 * @brief AuCredGetGroupID -- return a group
 * id number
 * @param string -- group name
 */
GID_NUM AuCredGetGroupID(uint8_t category) {
	if (category > AURORA_MAX_GROUPS)
		return 0;
	return _groups[category];
}

/**
 * @brief AuCredChangeID -- change credential ids
 * @param fd -- file descriptor
 * @param uid -- user id
 * @param gid -- groupd id number
 */
int AuCredChangeID(int fd, UID_NUM uid, GID_NUM gid) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);
	/* a secondary thread is not its process's main thread */
	if (!proc)
		proc = AuProcessFindSubThread(current_thr);

	if (!proc)
		return 1;

	/** check capabilities **/
	if (!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETUID) &&
		!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETGID))
		return 1;

	/* fd is a raw user value: it indexed fds[] with no bounds check */
	if (fd < 0 || fd >= FILE_DESC_PER_PROCESS)
		return 1;
	AuVFSNode* file = proc->fds[fd];
	if (!file)
		return 1;

	file->uid = uid;
	file->gid = gid;

	return 0;
}

/**
 * @brief AuCredAddSGroup -- add supplimentary gid to process
 * @param proc_id -- process id number
 * @param sgid -- sgid number
 */
int AuCredAddSGroup(int proc_id, int sgid) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);
	/* a secondary thread is not its process's main thread */
	if (!proc)
		proc = AuProcessFindSubThread(current_thr);

	if (!proc)
		return 1;

	/** check capabilities **/
	if (!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETGID))
		return 1;

	if (proc_id != 0)
		proc = AuProcessFindPID(proc_id);
	if (!proc)
		return 1;
	/* sgid[] holds AURORA_MAX_GROUPS entries; the index was never checked */
	if (proc->creds.num_sgid >= AURORA_MAX_GROUPS)
		return 1;

	proc->creds.sgid[proc->creds.num_sgid] = sgid;
	proc->creds.num_sgid++;

	return 0;
}

/**
 * AuCredSetCap -- add capability to a process
 * @param proc_id -- process id, zero for current process
 * @param cap -- capability to add
 */
int AuCredSetCap(int proc_id, int cap) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);
	/* a secondary thread is not its process's main thread */
	if (!proc)
		proc = AuProcessFindSubThread(current_thr);

	if (!proc)
		return 1;

	/** check capabilities **/
	if (!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETCAP))
		return 1;

	if (proc_id != 0)
		proc = AuProcessFindPID(proc_id);
	if (!proc)
		return 1;

	proc->creds.caps = cap;
	return 0;
}

/**
 * AuCredGetCap -- get capabilities of desired process
 * @param proc_id -- process id, zero for current process
 */
int AuCredGetCap(int proc_id) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);
	/* a secondary thread is not its process's main thread */
	if (!proc)
		proc = AuProcessFindSubThread(current_thr);

	if (!proc)
		return 1;

	/** check capabilities **/
	if (!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETCAP))
		return 1;

	/* zero means "this process"; the test was inverted and looked up pid 0 */
	if (proc_id != 0)
		proc = AuProcessFindPID(proc_id);
	if (!proc)
		return 1;

	return proc->creds.caps;
}

/**
 * @brief AuCredCheckPermissions -- before giving permission, 
 * check all credentials, enei aru access koriba dibi neki, 
 * ji pai take korba nori tu
 * @param node -- File node
 * @param cred -- Process's credential
 */
int AuCredCheckPermissions(AuVFSNode* node, AuProcCredentials* cred) {
	if (!node)
		return 1;
	/** root get access to everything */
	if (cred->uid == 0)
		return 0;
	/* /dev/graph is the normal user's stable desktop staging surface. It is
	 * deliberately not a hardware capability; keep the compositor and gpusrv
	 * usable even when the boot-time device node carries a stale/random group
	 * value. PCI and driver-control nodes remain group-gated below. */
	if ((node->flags & FS_FLAG_DEVICE) && !strcmp(node->filename, "graph") && cred->uid == 1000)
		return 0;

	/** maybe this process's user, created the file and owner
	 * of this file
	 */
	if (cred->uid == node->uid)
		return 0;

	/** maybe this process directly belong to the group */
	if (cred->gid == node->gid)
		return 0;

	/** now search for supplimentary gids **/
	for (int i = 0; i < cred->num_sgid; i++) {
		GID_NUM sgid = cred->sgid[i];
		if (sgid == node->gid)
			return 0;
	}

	AuTextOut("[aurora]: file : %s is not accessible to uid : %d \r\n", node->filename, cred->uid);
	/** sorry,kela, no access to the file **/
	return 1;
}

/**
 * @brief AuSetUID -- change user id of a process
 * @param proc -- Process id to change their uid
 * @param uid -- User id
 */
int AuSetUID(int proc_id, UID_NUM uid) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);

	if (!proc)
		proc = AuProcessFindSubThread(current_thr);

	if (!proc)
		return 1;

	if (!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETUID))
		return 1;

	/** 
	   *don't allow root process to change it's uid/gid, 
	    in Xeneva the root process get process id of one 
		*/
	if ((proc_id == 0 && proc->proc_id == 1)) {
		UARTDebugOut("[aurora]: root process is not allowed to change its uid \r\n");
		return 1;
	}

	if (proc_id != 0)
		proc = AuProcessFindPID(proc_id);
	if (!proc)
		return 1;

	if (proc_id != 0 && proc->proc_id == proc_id) {
		proc->creds.euid = uid;
		return 0;
	}

	proc->creds.uid = uid;
	return 0;
}

/**
 * @brief AuSetGID -- change user id of a process
 * @param proc -- Process id to change their uid
 * @param uid -- User id
 */
int AuSetGID(int proc_id, GID_NUM gid) {
	AA64Thread* current_thr = AuGetCurrentThread();
	AuProcess* proc = AuProcessFindThread(current_thr);

	if (!proc)
		proc = AuProcessFindSubThread(current_thr);

	if (!proc)
		return 1;

	if (!CRED_IS_CAPABLE(proc->creds.caps, CRED_CAP_SETGID)) {
		return 1;
	}

	/**
	   *don't allow root process to change it's uid/gid,
		in Xeneva the root process get process id of one
		*/
	if ((proc_id == 0 && proc->proc_id == 1)) {
		UARTDebugOut("[aurora]: root process is not allowed to change its uid \r\n");
		return 1;
	}

	if (proc_id != 0)
		proc = AuProcessFindPID(proc_id);
	if (!proc)
		return 1;

	/** suppose a normal user process call setgid, then only
	  its effective gid should change
	   */
	if (proc_id != 0 && proc_id == proc->proc_id) {
		proc->creds.egid = gid;
		return 0;
	}
	proc->creds.gid = gid;
	return 0;
}

/**
 * @brief AuCredIsRoot -- check if credential information
 * is in root
 * @param c -- Pointer to credential information
 */
bool AuCredIsRoot(AuProcCredentials* c) {
	return c->uid == 0;
}

/**
 * @brief AuCredInGroup -- check the credentials if it lies
 * in desired groupd
 * @param c -- Pointer to credential information
 * @param gid -- Group id to check 
 */
bool AuCredInGroup(AuProcCredentials* c, GID_NUM gid) {
	if (c->gid == gid)
		return true;
	for (uint16_t i = 0; i < c->num_sgid && i < AURORA_MAX_GROUPS; i++) {
		if (c->sgid[i] == gid)
			return true;
	}
	return false;
}