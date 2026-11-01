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

#include <FS/Fat/Fat.h>
#include <Fs/vfs.h>
#include <stdbool.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <stdint.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <string.h>
#include <Drivers/uart.h>

#define FAT_DIRENT_FREE 0xE5
#define FAT_DIRENT_END	0x00

uint16_t FatModeFromAttrib(uint8_t attrib, const char* name) {
	bool dir = (attrib & FAT_ATTRIBUTE_DIRECTORY) != 0;
	bool ro = (attrib & FAT_ATTRIBUTE_READ_ONLY) != 0;
	if (dir)
		return ro ? 0555 : 0755;
	const char* ext = strchr(name, '.');

	/* maybe we can have a table containing exe extensions */
	bool exe =
		ext && (strcmp(ext, ".exe") == 0 || strcmp(ext, ".elf") == 0 || strcmp(ext, ".dll") == 0);

	if (exe)
		return ro ? 0555 : 0755;
	return ro ? 0444 : 0644;
}

/**
 * @bried FatSetAttribute -- set attribute to directory entry
 * @param fsys -- Pointer to file system 
 * @param file -- Pointer to file which need attribute update
 */
int FatSetAttribute(AuVFSNode* fsys, AuVFSNode* file) {
	if (!fsys || !file)
		return -1;
	FatFS* fs = (FatFS*)fsys->device;
	AuVDisk* vdisk = (AuVDisk*)fs->vdisk;
	if (!vdisk)
		return -1;

	/* the root directory itself has no directory entry : nothing to store */
	if (file->first_block == fs->__RootDirFirstCluster && file->parent_block == 0)
		return 0;
	char dosname[11];
	FatToDOSFilename(file->filename, dosname, 11);

	uint32_t cluster = file->parent_block ? file->parent_block : fs->__RootDirFirstCluster;
	uint32_t ents_per_sector = fs->__BytesPerSector / sizeof(FatDir);

	/* page sized fat alloc cluster */
	uint8_t* buf = (uint8_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	if (!buf)
		return -1;

	int ret = -1;
	uint32_t guard = 0;

	while (cluster >= 2 && cluster < 0x0FFFFFF8 && guard++ < 65536) {
		uint16_t first_sector = FatClusterToSector32(fs, cluster);

		for (uint32_t s = 0; s < fs->__SectorPerCluster; s++) {
			uint64_t lba = first_sector + s;
			memset(buf, 0, fs->__BytesPerSector);
			AuVDiskRead(vdisk, lba, 1, (uint64_t*)buf);

			FatDir* d = (FatDir*)buf;
			for (uint32_t i = 0; i < ents_per_sector; i++, d++) {
				uint8_t first = (uint8_t)d->filename[0];
				if (first == FAT_DIRENT_END)
					goto done;
				if (first == FAT_DIRENT_FREE)
					continue;
				if ((d->attrib & FAT_ATTRIBUTE_LONG_NAME) == FAT_ATTRIBUTE_LONG_NAME)
					continue;
				if (strncmp(d->filename, dosname, 11) != 0)
					continue;

				uint32_t fc = ((uint32_t)d->first_cluster_hi_bytes << 16) | d->first_cluster;
				if (fc != file->first_block)
					continue;

				//actual change
				uint8_t attr = d->attrib;
				if (file->mode & 0222)
					attr &= ~FAT_ATTRIBUTE_READ_ONLY;
				else
					attr |= FAT_ATTRIBUTE_READ_ONLY;

				if (attr != d->attrib) {
					d->attrib = attr;
					aa64_data_cache_clean_range(buf, PAGE_SIZE);
					if (AuVDiskWrite(vdisk, lba, 1, (uint64_t*)buf) == 0) {
						UARTDebugOut("[fat-setattribute]: failed to write attribute : %x \r\n",
									 attr);
						ret = -1;
						goto done;
					}
					AuVDiskFlush(vdisk);
				}

				file->mode = (file->mode & ~0777) | FatModeFromAttrib(attr, file->filename);
				ret = 0;
				goto done;
			}
		}
		cluster = FatReadFAT(fsys, cluster);
	}

done:
	if (ret == 0)
		UARTDebugOut("[fat-setaatribute]: attribute : %x written successfully \r\n", file->mode);

	AuPmmngrReleasePage((uint64_t)V2P((size_t)buf));
}