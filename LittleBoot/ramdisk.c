/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

#include "ramdisk.h"
#include "dtb.h"
#include "physm.h"
#include "ctype.h"
#include "vmmngr.h"
#include "pe.h"
#include "aurora.h"

/*
 * Limited Ramdisk module, only load and read 
 * the main kernel file from directory EFI/XENEVA/
 * - EFI directory is used because same raw image
 * is used to boot xeneva through UEFI bootloader
 */

uint64_t ramdisk_start;
uint64_t ramdisk_end;
uint64_t sectorPerCluster;
uint64_t RootSector;
uint64_t ClusterBeginLBA;
uint64_t sectPerFAT32;
uint64_t fatBeginLBA;
uint64_t kernelSize;
#define SECTOR_SIZE 512

static void copy_mem(void* dst, void* src, size_t length){
    uint8_t* dstp = (uint8_t*)dst;
    uint8_t* srcp = (uint8_t*)src;
    while(length--)
       *dstp++ = *srcp++;
}

static void zero_mem(void* dst, size_t len){
    uint8_t* dstp = (uint8_t*)dst;
    while(len--)
       *dstp++ = 0;
}

void LBRamdiskReadSector(uint32_t lba,size_t counts,uint8_t* buffer){
    uint64_t offset = lba*SECTOR_SIZE;
    if (ramdisk_start + offset + (SECTOR_SIZE*counts) > ramdisk_end)
        return;
    const uint8_t* src = (const uint8_t*)(ramdisk_start + offset);
    for (int i = 0; i < SECTOR_SIZE*counts; i++)
        buffer[i] = src[i];
    asm volatile ("dsb ish\n");
}

struct __attribute__((packed)) FatBPB {
	uint8_t jmp[3];
	char    oemid[8];
	uint16_t bytes_per_sector;
	uint8_t  sectors_per_cluster;
	uint16_t reserved_sectors;
	uint8_t  num_fats;
	uint16_t num_dir_entries;
	uint16_t total_sectors_short;
	uint8_t  media_type;
	uint16_t sectors_per_fat;
	uint16_t sectors_per_track;
	uint16_t heads;
	uint32_t hidden_sectors;
	uint32_t large_sector_count;
	union {
		struct {
			uint8_t  drive_num;
			uint8_t  WinNtFlags;
			uint8_t  signature;
			uint32_t vol_serial_num;
			char     vol_label[11];
			char     sys_id[8];
		}FAT16;

		struct {
			uint32_t sect_per_fat32;
			uint16_t falgs;
			uint16_t fat_version;
			uint32_t root_dir_cluster;
			uint16_t fs_info_sect;
			uint16_t backup_boot_sect;
			uint8_t  reserved[12];
			uint8_t  drive_number;
			uint8_t  flagsWinNT;
			uint8_t  signature;
			uint32_t vol_serial_num;
			char     vol_label[11];
			char     sys_id[8];
		}FAT32;
	}info;
};

struct __attribute__ ((packed)) FatDir{
    uint8_t filename[11];
    uint8_t attrib;  //0xb
    uint8_t reserved; //0xD
    uint8_t time_created_ms; //0xE
    uint16_t time_created; //0xF
    uint16_t date_created; //0x11
    uint16_t date_last_accessed; //0x13
    uint16_t first_cluster_hi_bytes; //0x15
    uint16_t last_wrt_time; //0x17
    uint16_t last_wrt_date; //0x19
    uint16_t first_cluster; //0x21
    uint32_t file_size; //0x23
};

uint64_t FatClusterToSector(uint64_t cluster){
    return (ClusterBeginLBA + ((cluster - 2) * sectorPerCluster));
}

void FatToDOSFilename(const char* filename, char* fname, unsigned int fname_len){
    unsigned int i = 0;

    if (fname_len > 11)
       return;

    if (!fname || !filename)
       return;

    memset(fname, ' ', fname_len);

    for (i = 0; i < strlen(filename) && i < fname_len; i++){
        if (filename[i] == '.' || i == 8) break;
        fname[i] = toupper(filename[i]);
    }

    if (filename[i] == '.'){
        for (int k = 0; k < 3; k++){
            ++i;
            if (filename[i])fname[8+k] = filename[i];
        }
    }

    for(i = 0; i < 3; i++)
       fname[8+i] = toupper(fname[8+i]);
}

uint8_t ScratchPad[4096];

uint32_t ExtractEFICluster(){
    struct FatDir* dirent;
    char dos_file_name[12];
    FatToDOSFilename("EFI", dos_file_name, 11);
    dos_file_name[11] = 0;
    for (uint32_t sector = 0; sector < sectorPerCluster; sector++){
        memset(ScratchPad, 0, 4096);
        LBRamdiskReadSector(RootSector + sector,1,ScratchPad);
      
        uint64_t* alignedBuf = (uint64_t*)ScratchPad;
        dirent = (struct FatDir*)ScratchPad;
        for (int _num_entries = 0; _num_entries < 16; _num_entries++){
            LBUartPutString(dirent->filename);
            LBUartPutc('\r');
            if (strncmp(dos_file_name, dirent->filename,11) == 0){
                LBUartPutString("EFI directory found \n");
                LBUartPrintInt(dirent->first_cluster);
                LBUartPutString("\n");
                return dirent->first_cluster;
            }
            dirent++;
        }
    }

    LBUartPutString("Extraction completed \n");
    return 0;
}

uint32_t ExtractXeneva(uint32_t parentCluster){
    uint32_t EFIsector = FatClusterToSector(parentCluster);
    struct FatDir* dirent;
    char dos_file_name[12];
    FatToDOSFilename("XENEVA", dos_file_name, 11);
    dos_file_name[11] = 0;
    for (uint32_t sector = 0; sector < sectorPerCluster; sector++){
        memset(ScratchPad, 0, 4096);
        LBRamdiskReadSector(EFIsector + sector,1,ScratchPad);
      
        uint64_t* alignedBuf = (uint64_t*)ScratchPad;
        dirent = (struct FatDir*)ScratchPad;
        for (int _num_entries = 0; _num_entries < 16; _num_entries++){
            if (strncmp(dos_file_name, dirent->filename,11) == 0){
                LBUartPutString("XENEVA directory found \n");
                LBUartPrintInt(dirent->first_cluster);
                return dirent->first_cluster;
            }
            dirent++;
        }
    }

    LBUartPutString("Extraction completed \n");
    return 0;
}

uint64_t align_to_page(uint64_t size){
    return (size + 4096 - 1) & ~(4096 - 1);
}
uint32_t ExtractKernel(uint32_t parentClust){
    uint32_t EFIsector = FatClusterToSector(parentClust);
    struct FatDir* dirent;
    char dos_file_name[12];
    FatToDOSFilename("xnkrnl.exe", dos_file_name, 11);
    dos_file_name[11] = 0;
    for (uint32_t sector = 0; sector < sectorPerCluster; sector++){
        memset(ScratchPad, 0, 4096);
        LBRamdiskReadSector(EFIsector + sector,1,ScratchPad);
      
        dirent = (struct FatDir*)ScratchPad;
        for (int _num_entries = 0; _num_entries < 16; _num_entries++){
            if (strncmp(dos_file_name, dirent->filename, 11) == 0){
                LBUartPutString("xnkrnl found\n");
                LBUartPutString("kernel size :");
                LBUartPrintInt(dirent->file_size);
                kernelSize = align_to_page(dirent->file_size);
                return dirent->first_cluster;
            }
            dirent++;
        }
    }

    LBUartPutString("Extraction completed \n");
    return 0;
}

/*
 * FatReadFAT -- read the FAT data structure
 */
uint32_t FatReadFAT(uint64_t clusterIndex){
    uint64_t fat_offset = clusterIndex * 4;
    uint64_t fat_sector = fatBeginLBA + (fat_offset / SECTOR_SIZE);
    size_t ent_offset = fat_offset % SECTOR_SIZE;
    memset(ScratchPad, 0, 4096);
    LBRamdiskReadSector(fat_sector, 1, ScratchPad);
    uint32_t value = *(uint32_t*)&ScratchPad[ent_offset];
    return (value & 0x0FFFFFFF);
}

uint64_t kernelAddr = 0xFFFFC00000000000;
uint64_t loadAddr = 0xFFFFD00000000000;
uint64_t loadAddrLast;
#define FAT_EOC_MARK 0xFFFFFFF8
typedef void (*entry)();
entry ent;

void ReadKernelFile(uint32_t firstCluster){
    uint32_t sector = FatClusterToSector(firstCluster);
    uint64_t* addr = (uint64_t*)LBPmmngrAlloc();
    LBUartPutString("Kernel Physical address : ");
    LBUartPrintHex((uint64_t)addr);
    LBUartPutc('\r');

    LBPagingMap(0xFFFFD00000000000, (uint64_t)addr);
    LBRamdiskReadSector(sector, sectorPerCluster, (uint8_t*)addr);
    uint32_t clust;
    int index = 1;
    while(1){
        clust = FatReadFAT(firstCluster);
         if (clust >= (FAT_EOC_MARK & 0x0FFFFFFF))
               break;
        if (clust >= 0x0FFFFFF7)
               break;
        sector = FatClusterToSector(clust);
        addr = (uint64_t*)LBPmmngrAlloc();
        memset(addr, 0, 4096);
        LBPagingMap(loadAddr + index * 4096, (uint64_t)addr);
        LBRamdiskReadSector(sector, sectorPerCluster, (uint8_t*)addr);
        firstCluster = clust;
        index++;
    }
    loadAddrLast = loadAddr + index * 4096;
    LBUartPutString("Kernel file read completed \n");
    LBUartPutString("Kernel last read address :");
    LBUartPrintHex(loadAddrLast);
}

void LoadKernelFile(){
    /* Read and Map, Read and Map*/
    uint64_t buffer = (uint64_t)LBPmmngrAlloc();
    LBPagingMap(kernelAddr, buffer);

    IMAGE_DOS_HEADER * dos = (IMAGE_DOS_HEADER*)loadAddr;
    IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)((size_t)dos + dos->e_lfanew);
    IMAGE_SECTION_HEADER *sect = (IMAGE_SECTION_HEADER*)((size_t)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionaHeader);
    
    copy_mem((void*)kernelAddr,(void*)loadAddr,4096);

    for (size_t i = 0; i < nt->FileHeader.NumberOfSections; ++i){
        size_t sectAddr = kernelAddr + sect[i].VirtualAddress;
        size_t sectsz = sect[i].VirtualSize;
        int req_pages = sectsz/4096 + ((sectsz % 4096) ? 1 : 0);
        for (int j = 0; j < req_pages; j++){
            uint64_t alloc = (sectAddr + j * 4096);
            paddr_t* addr = (paddr_t*)LBPmmngrAlloc();
            memset(addr, 0, 4096);
            LBPagingMap((sectAddr + j * 0x1000), (uint64_t)addr);
            memset((void*)alloc, 0, 4096);
        }
        copy_mem((void*)sectAddr,RAW_OFFSET(void*,loadAddr,sect[i].PointerToRawData),sect[i].SizeOfRawData);
        if (sect[i].VirtualSize > sect[i].SizeOfRawData)
           zero_mem(RAW_OFFSET(void*,sectAddr, sect[i].SizeOfRawData),sect[i].VirtualSize - 
        sect[i].SizeOfRawData);

    }
    /* caches are on now: the image was written through the D-cache and
     * must be made visible to instruction fetch before it runs */
    LBCacheSyncICache(kernelAddr, kernelAddr + nt->OptionalHeader.SizeOfImage);
    ent = (entry)(kernelAddr + nt->OptionalHeader.AddressOfEntryPoint);
    LBUartPrintHex((uint64_t)ent);
}
/*
 * LBRamdiskLocate -- find the initramfs the firmware loaded, from
 * /chosen linux,initrd-start/end. Called before the memory map is built
 * so the initramfs can be kept out of LittleBoot's allocator.
 * @return 1 if found
 */
int LBRamdiskLocate(){
    uint32_t* rmdisk = dtb_find_node_prefix("chosen");
    if (!rmdisk){
       LBUartPutString("Ramdisk: no /chosen node \n");
       return 0;
    }
    uint32_t *initrd_start = dtb_node_find_property(rmdisk, "linux,initrd-start");
    uint32_t *initrd_end = dtb_node_find_property(rmdisk,"linux,initrd-end");
    if (!initrd_start || !initrd_end){
       LBUartPutString("Ramdisk: /chosen has no linux,initrd-start/end \n");
       return 0;
    }
    /* initrd_start[0] is the property length: 4 bytes = one cell
     * (Pi firmware / QEMU on BCM2837), 8 bytes = two cells */
    ramdisk_start = LBDTBPropReadCells(initrd_start, 0, LBDTBPropLength(initrd_start) / 4);
    ramdisk_end = LBDTBPropReadCells(initrd_end, 0, LBDTBPropLength(initrd_end) / 4);
    LBUartPutString("IRD Start:");
    LBUartPrintHex(ramdisk_start);
    LBUartPutString("IRD END:");
    LBUartPrintHex(ramdisk_end);
    return ramdisk_start != 0 && ramdisk_end > ramdisk_start;
}

/*
 * LBRamdiskInitialize -- mount the initramfs located by LBRamdiskLocate
 * and load EFI/XENEVA/xnkrnl.exe from it
 */
void LBRamdiskInitialize(){
    if (ramdisk_start == 0 || ramdisk_end <= ramdisk_start){
       LBUartPutString("Ramdisk not found \n");
       return;
    }
    LBUartPutString("Ramdisk initialized \n");

    paddr_t* addr = (paddr_t*)ScratchPad;
    LBRamdiskReadSector(0,8,addr);
    struct FatBPB *bpb = (struct FatBPB*)addr;
    for (int i = 0; i < 3; i++){
        LBUartPutc(bpb->jmp[i]);
    }
    
    LBUartPutc('\r');
    for (int i = 0; i < 8; i++){
        LBUartPutc(bpb->oemid[i]);
    }
    LBUartPutString("\n");
    sectorPerCluster = bpb->sectors_per_cluster;
    ClusterBeginLBA = bpb->reserved_sectors + (bpb->num_fats* bpb->info.FAT32.sect_per_fat32);
    sectPerFAT32 = bpb->info.FAT32.sect_per_fat32;
    fatBeginLBA = bpb->reserved_sectors;
    RootSector = FatClusterToSector(bpb->info.FAT32.root_dir_cluster);
    memset(ScratchPad, 0, 4096);
    LBUartPutString("Root cluster :");
    LBUartPrintInt(bpb->info.FAT32.root_dir_cluster);
    LBUartPutc('\r');
    LBUartPutString("Root sector :");
    LBUartPrintInt(RootSector);
     LBUartPutc('\r');
    LBUartPutString("Number of reserved sector :");
    LBUartPrintInt(bpb->reserved_sectors);
     LBUartPutc('\r');
    LBUartPutString("Sectot per fat32 :");
    LBUartPrintInt(bpb->info.FAT32.sect_per_fat32);
    LBUartPutc('\r');
    LBUartPutString("Sectors per cluster:");
    LBUartPrintInt(sectorPerCluster);
    LBUartPutc('\r');
    uint32_t efiClust = ExtractEFICluster();
    uint32_t kernelClust = ExtractXeneva(efiClust);
    uint32_t xnkrnlClust = ExtractKernel(kernelClust);
    ReadKernelFile(xnkrnlClust);
    LoadKernelFile();
    /* the raw file copy at loadAddr is no longer needed: unmap it and give
     * the pages back, so the kernel neither inherits the mapping (it maps
     * the framebuffer right above it) nor loses the memory */
    for (uint64_t j = loadAddr; j < loadAddrLast; j += 0x1000)
        LBPagingFree(j, 1);
    LBUartPutString("Kernel staging area released \n");
}

/*
 * Jumps to kernel entry point
 */
void LBRamdiskJumpToKernel(uint64_t stack, size_t stacksz,KERNEL_BOOT_INFO* info){ 
    uint64_t bootinfo = (uint64_t)info;
      LBUartPrintHex(bootinfo);
    /*
     * Stack switch, argument and branch must be one asm block: as separate
     * statements followed by a C call to ent(), the compiler is free to
     * reuse x0 and to tail-call (clang emits "ldr x0, ent; br x0"), so the
     * kernel would get its own entry address instead of the boot info.
     * x0 = KERNEL_BOOT_INFO*, x1 = 30 (as before), fresh frame chain.
     */
    __asm__ volatile(
        "add x5, %0, %1\n"
        "sub x5, x5, #64\n"
        "mov sp, x5\n"
        "mov x29, xzr\n"
        "mov x30, xzr\n"
        "mov x0, %2\n"
        "mov x1, #30\n"
        "br %3\n"
        :
        :"r"(stack),"r"(stacksz),"r"(bootinfo),"r"(ent)
        : "x0", "x1", "x5", "x29", "x30", "memory"
    );
    __builtin_unreachable();
}

uint64_t LBRamdiskGetStartAddress(){
    return ramdisk_start;
}

uint64_t LBRamdiskGetEndAddress(){
    return ramdisk_end;
}



