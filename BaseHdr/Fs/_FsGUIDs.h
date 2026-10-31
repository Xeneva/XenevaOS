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

#ifndef __FS_GUIDS_H__
#define __FS_GUIDS_H__

#include <stdint.h>

#pragma pack (push,1)
typedef struct _GUID {
	uint32_t Data1;
	uint16_t Data2;
	uint16_t Data3;
	uint8_t Data4[8];
}GUID;
#pragma pack(pop)


/* Here goes Ext2/3/4 file systems */
#define LINUX_HOME_PARTITION_GUID \
   {\
   0x933AC7E1, 0x2eb4, 0x4F13, { 0xB8, 0x44, 0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15 }\
   }
 
/* Here also goes Ext2/3/4,XFS..etc filesystems*/
#define LINUX_FILESYSTEM \
   {\
    0x0FC63DAF, 0x8483, 0x4772, {0x79,0x8E, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4} \
   }

/* Here goes FAT32/16/12 or NTFS file systems */
#define WINDOWS_PARTITION_DATA_GUID \
	{\
	0xEBD0A0A2, 0xB9E5, 0x4433, { 0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7 }\
	}


#endif