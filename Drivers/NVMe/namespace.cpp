/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2024, Manas Kamal Choudhury
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

#include "namespace.h"
#include "nvme.h"
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Hal/serial.h>
#include <aucon.h>
#include <stdio.h>
#include <Fs/vdisk.h>
#include <Fs/Dev/devfs.h>

enum NVMCommands{
	NVMCmdFlush = 0x0,
	NVMCmdWrite = 0x1,
	NVMCmdRead = 0x2,
};

/*
 * NVMeNamespaceRead -- read data from nvme namespace
 * @param namespc_ -- pointer to NVMe namespace
 * @param lba -- starting lba
 * @param count -- number of lba to read
 * @param buffer -- buffer, where to copy data
 */
int NVMeNamespaceRead(NVMeNamespace* namespc_, uint64_t lba, uint32_t count, uint64_t* buffer){
	if (lba + count > namespc_->maxBlocks)
		return 1;

	uint32_t blockCount = (count + (namespc_->blockSize - 1)) / namespc_->blockSize;
	uint8_t* alignedBuff = (uint8_t*)buffer;
	NVMeQueue* ioqe = NVMeGetQueue(1);
	if (!ioqe) {
		SeTextOut("[NVMe]: error I/O queue not found \r\n");
		return 1;
	}

	memset((void*)namespc_->physMMIOBuffer, 0, PAGE_SIZE);

	NVMeCompletion comp;
	NVMeCommand cmd;
	memset(&cmd, 0, sizeof(NVMeCommand));
	cmd.opcode = (NVMCommands::NVMCmdRead & UINT8_MAX);
	cmd.read.blockNum = 1;
	cmd.read.startLBA = lba;
	cmd.prp1 = (namespc_->physDataBuffer & UINT64_MAX);
	cmd.nsid = namespc_->nsID;

	while (blockCount) {
		int sz = namespc_->blockSize;

		NVMeSubmitCommand(ioqe, &cmd, &comp);

		if (comp.status > 0) {
			SeTextOut("[NVMe]: Read error status %x NSID -> %d \r\n", comp.status, 
				namespc_->nsID);
			return 1;
		}
		SeTextOut("[NVMe] : reading block %d sz -> %d \r\n", lba, sz);

		memcpy(alignedBuff, (void*)namespc_->physMMIOBuffer, sz);
		alignedBuff += sz;
		cmd.read.startLBA++;
		blockCount--;
	}
	return 0;
}

/*
 * NVMeNamespaceWrite -- write data to NVMe namespace
 * @param namespc -- Pointer to namespace
 * @param lba -- Starting lba
 * @param count -- number of lba to write to
 * @param buffer -- Buffer to copy
 */
int NVMeNamespaceWrite(NVMeNamespace *namespc, uint64_t lba, uint32_t count, uint64_t* buffer) {
	if (lba + count > namespc->maxBlocks)
		return 1;

	uint32_t blockCount = (count + (namespc->blockSize - 1)) / namespc->blockSize;

	uint8_t* alignedBuff = (uint8_t*)buffer;
	NVMeQueue* ioqe = NVMeGetQueue(1);
	if (!ioqe) {
		SeTextOut("[NVMe]: error I/O queue not found \r\n");
		return 1;
	}

	NVMeCompletion comp;
	NVMeCommand cmd;
	memset(&cmd, 0, sizeof(NVMeCommand));
	cmd.opcode = (NVMCommands::NVMCmdWrite & UINT8_MAX);
	cmd.write.blockNum = 1;
	cmd.write.startLBA = lba;
	cmd.prp1 = (namespc->physDataBuffer & UINT64_MAX);
	cmd.nsid = namespc->nsID;
	
	while (blockCount) {
		int sz = namespc->blockSize;

		memcpy((void*)namespc->physMMIOBuffer,alignedBuff, sz);

		NVMeSubmitCommand(ioqe, &cmd, &comp);

		if (comp.status > 0) {
			SeTextOut("[NVMe]: write error status %d nsID -> %d \r\n", comp.status, namespc->nsID);
			return 1;
		}

		alignedBuff += sz;
		cmd.write.startLBA++;
		blockCount--;
	}
	return 0;
}

/*
 * NVMeReadBlock -- vdisk read callback 
 */
int NVMeReadBlock(AuVDisk* vdisk, uint64_t lba, uint32_t count, uint64_t* buffer){
	NVMeNamespace* namespace_ = (NVMeNamespace*)vdisk->data;
	if (namespace_)
		return NVMeNamespaceRead(namespace_, lba, count, buffer);
	return 1;
}

/*
 * NVMeWriteBlock -- vdisk write callback 
 */
int NVMeWriteBlock(AuVDisk* vdisk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	NVMeNamespace* namespace_ = (NVMeNamespace*)vdisk->data;
	if (namespace_)
		return NVMeNamespaceWrite(namespace_, lba, count, buffer);
	return 1;
}


size_t NVMeFileWrite(AuVFSNode* _node_, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	if (!file)
		return 0;
	uint64_t lba = file->pos / 512;
	uint64_t lba_bytes = static_cast<uint64_t>(len) * 512;
	AuVDisk* vdisk = (AuVDisk*)file->device;
	if (!vdisk)
		return 0;
	NVMeWriteBlock(vdisk, lba, len, buffer);
	return lba_bytes;
}

size_t NVMeFileRead(AuVFSNode* _node_, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	if (!file)
		return 0;
	uint64_t lba = file->pos / 512;
	uint64_t lba_bytes = static_cast<uint64_t>(len) * 512;
	AuVDisk* vdisk = (AuVDisk*)file->device;
	if (!vdisk)
		return 0;
	NVMeReadBlock(vdisk, lba, len, buffer);
	return lba_bytes;
}
/*
 * NVMeInitialiseNamespace -- initialise and register a NVMe namespace
 * @param controller -- controller information
 * @param ni -- Namespace information
 * @param id -- Namespace id
 */
void NVMeInitialiseNamespace(NVMeDev* nvme, NVMeControllerIdentity* controller, NamespaceIdentity *ni, int id) {
	uint8_t lbaFormat = ni->fmtLBASize & 0xf;
	uint8_t lbaSize = ni->lbaFormat[lbaFormat].lbaDataSize;
	if (lbaSize >= 9) {
		NVMeNamespace *namespace_ = (NVMeNamespace*)kmalloc(sizeof(NVMeNamespace));
		int blockSize = (1 << lbaSize);
		uint64_t diskSizeInBytes = (ni->namespaceSize * blockSize);
		uint64_t diskSizeInGiB = (diskSizeInBytes / 1024 / 1024 / 1024);
		uint64_t diskSizeInMiB = (diskSizeInBytes / 1024 / 1024);
		AuTextOut("[NVMe]: Namespace Size : %d GiB, %d MiB \r\n", diskSizeInGiB, diskSizeInMiB);
		namespace_->totalSizeInBytes = diskSizeInBytes;
		namespace_->totalSizeInGiB = diskSizeInGiB;
		namespace_->nsID = id;
		namespace_->maxBlocks = ni->namespaceSize;
		namespace_->totalSizeInMiB = diskSizeInMiB;
		namespace_->blockSize = blockSize;
		namespace_->physDataBuffer = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		namespace_->physMMIOBuffer = (uint64_t)AuMapMMIO(namespace_->physDataBuffer, 1);
		memset((void*)namespace_->physMMIOBuffer, 0, PAGE_SIZE);

		AuVDisk *vdisk = (AuVDisk*)kmalloc(sizeof(AuVDisk));
		memset(vdisk, 0, sizeof(AuVDisk));
		memcpy(vdisk->diskname,(void*)controller->modelNumber, 40);
		memcpy(vdisk->serialNumber,(void*)controller->serialNumber, 20);
		vdisk->data = namespace_;
		vdisk->max_blocks = namespace_->maxBlocks;
		vdisk->blockSize = blockSize;
		vdisk->Write = NVMeWriteBlock;
		vdisk->Read = NVMeReadBlock;
		AuTextOut("[NVMe]: VDisk registered as %s,serial -> %s \n", vdisk->diskname,
			vdisk->serialNumber);

		int diskID = id - 1;
		char filename[32];
		strcpy(filename, "nvme");
		int offset = strlen(filename);
		sztoa(diskID, filename + offset, 10);

		/* now store the entire path in vdisk */
		strcpy(vdisk->diskPath, nvme->nvmedevpath);
		offset = strlen(vdisk->diskPath);
		strcpy(vdisk->diskPath + offset, "/");
		offset = strlen(vdisk->diskPath);
		strcpy(vdisk->diskPath + offset, filename);

		AuVDiskRegister(vdisk);

		AuVFSNode* diskfile = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
		memset(diskfile, 0, sizeof(AuVFSNode));
		strcpy(diskfile->filename, filename);
		diskfile->device = vdisk;
		diskfile->flags = FS_FLAG_DEVICE;
		diskfile->write = NVMeFileWrite;
		diskfile->read = NVMeFileRead;

		AuDevFSAddFile(nvme->devfs, nvme->nvmedevpath, diskfile);
		
	}
	
}