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

#ifndef __GPT_H__
#define __GPT_H__

#include <stdint.h>
#include <aurora.h>
#include <Fs/vdisk.h>
#include <Fs/_FsGUIDs.h>

#define BE_PREPROCESSOR16(x) \
	((x >> 8) | ((x & 0xFF) << 8))

#define CREATE_GUID(a, b, c, d, e0, e1, e2, e3, e4, e5) \
{a, b, c, { { BE_PREPROCESSOR16(d), { e0, e1, e2, e3, e4, e5 } } }}


#pragma pack(push,1)
typedef struct _GPT_HEADER_ {
	char sig[8];
	uint32_t rev;
	uint32_t hdr_sz;
	uint32_t crc32;
	uint32_t resv;
	uint64_t curr_lba;
	uint64_t backup_lba;
	uint64_t first_useable_lba;
	uint64_t last_useable_lba;
	uint64_t disk_guid[2];
	uint64_t part_table_lba;
	uint32_t num_part_entries;
	uint32_t size_of_part;
	uint32_t crc32_of_part_entries;
}GPTHeader;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct _GPT_Partition_ {
	GUID part_guid;
	GUID part_unique_guid;
	uint64_t first_lba;
	uint64_t last_lba;
	uint64_t attributes;
	char     part_name[72];
}GPTPartition;
#pragma pack(pop)

/* AuGUIDVerify -- compares two guid and return the result
* @param g1 -- GUID one
* @param g2 -- GUID two
*/
AU_EXTERN AU_EXPORT bool AuGUIDVerify(GUID g1, GUID g2);
/*
* AuGPTInitialise_FileSystem -- initialises specific file system
* by looking partition GUID
* @param vdisk -- Virtual disk partition
*/
extern void AuGPTInitialise_FileSystem(AuVDisk *vdisk);
#endif