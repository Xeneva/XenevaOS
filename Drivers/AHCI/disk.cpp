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
#include "disk.h"
#include "ahci.h"
#include <Mm/vmmngr.h>
#include <Mm/pmmngr.h>
#include <stdio.h>
#include <aucon.h>
#include <string.h>
#include <Fs/vdisk.h>
#include <Mm/kmalloc.h>
#include <Fs/Dev/devfs.h>
#include <Hal/serial.h>
#include <Hal/x86_64_sched.h>
#include <Hal/x86_64_lowlevel.h>


void AHCISleep(uint32_t ms) {
	uint32_t tick = ms + AuGetSystemTimerTick();
	while (tick > AuGetSystemTimerTick())
		;
}
/*
 * AuAHCIStopCmd -- stops ahci dma engine
 * @param port - sata drive port
 */
void AuAHCIStopCmd(HBA_PORT* port) {
	port->cmd &= ~PX_CMD_START;
	port->cmd &= ~PX_CMD_FRE;
	while (1) {
		if (port->cmd & PX_CMD_FR)
			continue;
		if (port->cmd & PX_CMD_CR)
			continue;
		break;
	}
}

/*
 * AuAHCIStartCmd -- starts command dma engine
 * @param port -- sata drive port
 */
void AuAHCIStartCmd(HBA_PORT* port) {
	while (port->cmd & PX_CMD_CR)
		;
	port->cmd |= PX_CMD_FRE;
	port->cmd |= PX_CMD_START;
}

uint32_t AuAHCIDiskFindSlot(HBA_PORT* port) {
	uint32_t slots = (port->sact | port->ci);
	for (int i = 0; i < 32; i++) {
		if ((slots & 1) == 0)
			return i;
		slots >>= 1;
	}
}

void AuAHCIDiskRead(HBA_PORT* port, uint64_t lba, uint32_t count, uint64_t* buffer) {
	int spin = 0;
	HBA_CMD_HEADER* cmd_list = (HBA_CMD_HEADER*)P2V(port->clb);
	uint64_t buffer_while = (uint64_t)buffer;

	cmd_list->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
	cmd_list->w = 0;
	cmd_list->prdtl = 1;

	uint32_t cmd_slot = AuAHCIDiskFindSlot(port);

	HBA_CMD_TABLE* tbl = (HBA_CMD_TABLE*)P2V(cmd_list[cmd_slot].ctba);

	int i = 0;
	tbl->prdt[0].data_base_address = buffer_while & UINT32_MAX;
	tbl->prdt[0].dbau = (buffer_while >> 32) & UINT32_MAX;
	tbl->prdt[0].data_byte_count = 512 * count - 1;
	tbl->prdt[0].i = 1;

	FIS_REG_H2D* fis = (FIS_REG_H2D*)tbl->cmd_fis;
	memset(fis, 0, sizeof(FIS_REG_H2D));
	fis->fis_type = FIS_TYPE_REG_H2D;
	fis->c = 1;
	fis->command = ATA_CMD_READ_DMA_EXT;
	fis->lba0 = lba & 0xff;
	fis->lba1 = (lba >> 8) & 0xff;
	fis->lba2 = (lba >> 16) & 0xff;
	fis->device = 1 << 6;
	fis->lba3 = (lba >> 24) & 0xff;
	fis->lba4 = (lba >> 32) & 0xff;
	fis->lba5 = (lba >> 40) & 0xff;
	fis->countl = count & 0xff;
	fis->counth = (count >> 8) & 0xff;

	while ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ))) {
		spin++;
	}

	port->ci = 1 << cmd_slot;
	while (1) {
		if ((port->ci & (1 << cmd_slot)) == 0)
			break;
		if (port->is & (1 << 30)) {
			break;
		}
	}

	while (port->tfd & (ATA_SR_BSY | ATA_SR_DRQ))
		;

}


/*
* AuAHCIDiskWrite -- Writes data to SATA disk
* @param port -- SATA port
* @param lba -- LBA value
* @param count -- number of sectors to use
* @param buffer -- memory buffer
*/
void AuAHCIDiskWrite(HBA_PORT* port, uint64_t lba, uint32_t count, uint64_t* buffer) {
	int spin = 0;
	
	HBA_CMD_HEADER* cmd_list = (HBA_CMD_HEADER*)P2V(port->clb);
	uint64_t buffer_whole = (uint64_t)buffer;

	cmd_list->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
	cmd_list->w = 1;
	cmd_list->prdtl = 1;

	uint32_t command_slot = AuAHCIDiskFindSlot(port);

	HBA_CMD_TABLE* tbl = (HBA_CMD_TABLE*)P2V(cmd_list[command_slot].ctba);

	int i = 0;
	tbl->prdt[0].data_base_address = buffer_whole & 0xffffffff;
	tbl->prdt[0].dbau = buffer_whole >> 32;
	tbl->prdt[0].data_byte_count = 512 * count - 1;
	tbl->prdt[0].i = 1;

	FIS_REG_H2D* fis = (FIS_REG_H2D*)tbl->cmd_fis;
	memset(fis, 0, sizeof(FIS_REG_H2D));
	fis->fis_type = FIS_TYPE_REG_H2D;
	fis->c = 1;
	fis->command = ATA_CMD_WRITE_DMA_EXT;
	fis->lba0 = lba & 0xff;
	fis->lba1 = (lba >> 8) & 0xff;
	fis->lba2 = (lba >> 16) & 0xff;
	fis->device = 1 << 6;
	fis->lba3 = (lba >> 24) & 0xff;
	fis->lba4 = (lba >> 32) & 0xff;
	fis->lba5 = (lba >> 40) & 0xff;
	fis->countl = count & 0xff;
	fis->counth = (count >> 8) & 0xff;
	fis->control = 0x8;

	while ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ)) && spin < 1000000) {
		spin++;
	}
	if (spin == 1000000)
		AuTextOut("[AHCI]:Port Hung\n");

	port->ci = 1 << command_slot;
	while (1) {
		if ((port->ci & (1 << command_slot)) == 0)
			break;
		if (port->is & (1 << 30)) {
			SeTextOut("[AHCI]: Port error \r\n");
			break;
		}
	}

	while (port->tfd & (ATA_SR_BSY | ATA_SR_DRQ))
		;

}



/*
* AuAHCIDiskIdentify -- identify the disk information
* @param port -- SATA Drive port
* @param lba -- Logical Block Address, for identification set this to 0
* @param count -- count --> 1
* @param buffer -- memory area to store the information
*/
void AuAHCIDiskIdentify(HBA_PORT* port, uint64_t lba, uint32_t count, uint64_t* buffer) {
	int spin = 0;
	HBA_CMD_HEADER* cmd_list = (HBA_CMD_HEADER*)port->clb;
	uint64_t buffer_whole = (uint64_t)buffer;

	cmd_list->cfl = sizeof(FIS_REG_H2D) / sizeof(uint32_t);
	cmd_list->w = 0;
	cmd_list->prdtl = (uint16_t)((count - 1) >> 4) + 1;

	uint32_t command_slot = AuAHCIDiskFindSlot(port);

	HBA_CMD_TABLE* tbl = (HBA_CMD_TABLE*)cmd_list[command_slot].ctba;
	int i = 0;
	uint8_t* buffer_aligned = (uint8_t*)buffer;
	for (i = 0; i < cmd_list->prdtl; i++) {
		tbl->prdt[i].data_base_address = buffer_whole & 0xffffffff;
		tbl->prdt[i].dbau = buffer_whole >> 32;
		tbl->prdt[i].data_byte_count = (512 * count) - 1;
		tbl->prdt[i].i = 1;
		buffer_aligned += 512 * static_cast<uint64_t>(count);
		buffer_whole = (uint64_t)buffer_aligned;
	}
	tbl->prdt[i].data_base_address = buffer_whole & 0xffffffff;
	tbl->prdt[i].dbau = buffer_whole >> 32;
	tbl->prdt[i].data_byte_count = (512 * count) - 1;
	tbl->prdt[i].i = 1;

	FIS_REG_H2D* fis = (FIS_REG_H2D*)tbl->cmd_fis;
	fis->fis_type = FIS_TYPE_REG_H2D;
	fis->c = 1;
	fis->command = ATA_CMD_IDENTIFY;
	fis->lba0 = lba & 0xff;
	fis->lba1 = (lba >> 8) & 0xff;
	fis->lba2 = (lba >> 16) & 0xff;
	fis->device = 1 << 6;
	fis->lba3 = (lba >> 24) & 0xff;
	fis->lba4 = (lba >> 32) & 0xff;
	fis->lba5 = (lba >> 40) & 0xff;
	fis->countl = count & 0xff;
	fis->counth = (count >> 8) & 0xff;

	while ((port->tfd & (ATA_SR_BSY | ATA_SR_DRQ)) && spin < 1000000) {
		spin++;
	}
	if (spin == 1000000)
		AuTextOut("[AHCI]:Port Hung\n");

	port->ci = 1 << command_slot;
	while (1) {
		if ((port->ci & (1 << command_slot)) == 0)
			break;
		if (port->is & (1 << 30))
			break;
	}
}


int AuAHCIVDiskRead(AuVDisk* disk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	HBA_PORT* port = (HBA_PORT*)disk->data;
	AuAHCIDiskRead(port, lba, count, buffer);
	return count;
}

int AuAHCIVDiskWrite(AuVDisk* disk, uint64_t lba, uint32_t count, uint64_t* buffer) {
	HBA_PORT* port = (HBA_PORT*)disk->data;
	AuAHCIDiskWrite(port, lba, count, buffer);
	return count;
}

size_t AHCIDiskWrite(AuVFSNode* _node_, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	/* here avoid using _node_ as file, because it contains the
	 * address to VDisk structure */
	if (!file)
		return 0;

	if (len > PAGE_SIZE)
		return 0;

    uint64_t lba = file->pos / 512;
	uint64_t lba_bytes = static_cast<uint64_t>(len) * 512;
	AuVDisk* disk = (AuVDisk*)file->device;
	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buff, 0, PAGE_SIZE);
	memcpy(buff, buffer, lba_bytes);
	AuAHCIVDiskWrite(disk, lba, len,(uint64_t*)V2P((size_t)buff));
	AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
	return lba_bytes;
}

size_t AHCIDiskRead(AuVFSNode* _node_, AuVFSNode* file, uint64_t* buffer, uint32_t len) {
	/* here avoid using _node_ as file, because it contains the
	 * address to VDisk structure */
	if (!file)
		return 0;

	if (len > PAGE_SIZE)
		return 0;

	uint64_t lba = file->pos / 512;
	uint64_t lba_bytes = static_cast<uint64_t>(len) * 512;
	AuVDisk* disk = (AuVDisk*)file->device;
	uint64_t* buff = (uint64_t*)P2V((size_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL));
	memset(buff, 0, PAGE_SIZE);
	AuAHCIVDiskRead(disk, lba, len, (uint64_t*)V2P((size_t)buff));
	memcpy(buffer,buff, lba_bytes);
	
	AuPmmngrReleasePage((uint64_t)V2P((size_t)buff));
	return lba_bytes;
}

/*
* AuAHCIDiskInitialise -- initialize the ahci sata disk
* @param port -- SATA Drive Port memory
* @param slot -- slot number
*/
void AHCIDiskInitialise(AHCIController *controller,HBA_PORT* port) {

	/* stop the DMA engine */
	AuAHCIStopCmd(port);

	uint64_t phys;

	/* Allocate command list */
	phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	port->clb = phys & 0xffffffff;
	port->clbu = phys >> 32;

	HBA_CMD_HEADER* cmd_list = (HBA_CMD_HEADER*)phys;
	memset((void*)phys, 0, 4096);

	/* Allocate FIS */
	phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	port->fb = phys & 0xffffffff;
	port->fbu = (phys >> 32);

	HBA_FIS* fis_dev = (HBA_FIS*)phys;
	memset((void*)phys, 0, 4096);

	uint8_t cold_presence = port->cmd & (1 << 20);
	if (cold_presence) {
		AuTextOut("ahci Port Supports cold presence %d\n", cold_presence);
	}

	for (int i = 0; i < 31; i++) {
		cmd_list[i].prdtl = 1;
		phys = (uint64_t)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
		cmd_list[i].ctba = phys & 0xffffffff;
		cmd_list[i].ctbau = phys >> 32;
		cmd_list[i].p = 1;
		cmd_list[i].cfl = 0x10;
		memset((void*)phys, 0, 4096);
	}

	port->serr = 0xffffffff;

	port->cmd &= ~HBA_PX_CMD_ICC;
	port->cmd |= PX_CMD_POD | PX_CMD_SUD | HBA_PX_CMD_ICC_ACTIVE;


	port->ie = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) |
		(1 << 7) | (1 << 22) | (1 << 23) | (1 << 24) | (1 << 26) | (1 << 27) | (1 << 28) |
		(1 << 29) | (1 << 30) | (1 << 31);

	/* start the command DMA engine */
	AuAHCIStartCmd(port);

	uint8_t current_slot = port->cmd & (1 << 8);

	uint64_t* addr = (uint64_t*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	memset(addr, 0, 4096);
	AuAHCIDiskIdentify(port, 0, 1, addr);
	char ata_device_name[40];
	uint8_t* ide_buf = (uint8_t*)addr;
	uint16_t* aligned_buf = (uint16_t*)addr;
	for (int i = 0; i < 40; i += 2)
	{
		ata_device_name[i] = ide_buf[54 + i + 1];
		ata_device_name[i + 1] = ide_buf[54 + i];
	}
	uint64 max_sectors = 0;

	/* not correct */
	uint16_t offset_83 = aligned_buf[83];

	if (offset_83 & (1 << 10)) {
		max_sectors = (uint16_t)aligned_buf[100];
	}
	else {
		max_sectors = aligned_buf[60] + aligned_buf[61];
	}

	AuVDisk* disk = AuCreateVDisk();
	strcpy(disk->diskname, ata_device_name);
	disk->data = port;
	disk->Read = AuAHCIVDiskRead;
	disk->Write = AuAHCIVDiskWrite;
	disk->max_blocks = 0;
	disk->currentLBA = 0;
	

	int diskID = controller->CurrentPortID;
	char filename[32];
	strcpy(filename, "ahci");
	int offset = strlen(filename);
	sztoa(diskID, filename + offset, 10);

	strcpy(disk->diskPath, controller->controllerpath);
	offset = strlen(disk->diskPath);
	strcpy(disk->diskPath + offset, "/");
	offset = strlen(disk->diskPath);
	strcpy(disk->diskPath + offset, filename);

	AuVDiskRegister(disk);

	AuVFSNode* file = (AuVFSNode*)kmalloc(sizeof(AuVFSNode));
	memset(file, 0, sizeof(AuVFSNode));
	strcpy(file->filename, filename);
	file->flags = FS_FLAG_DEVICE;
	file->device = disk;
	file->write = AHCIDiskWrite;
	file->read = AHCIDiskRead;

	AuDevFSAddFile(controller->devfs, controller->controllerpath, file);
	controller->CurrentPortID++;

	AuPmmngrReleasePage((uint64_t)addr);
}