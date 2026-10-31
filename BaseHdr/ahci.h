///**
//* BSD 2-Clause License
//*
//* Copyright (c) 2022-2023, Manas Kamal Choudhury
//* All rights reserved.
//*
//* Redistribution and use in source and binary forms, with or without
//* modification, are permitted provided that the following conditions are met:
//*
//* 1. Redistributions of source code must retain the above copyright notice, this
//*    list of conditions and the following disclaimer.
//*
//* 2. Redistributions in binary form must reproduce the above copyright notice,
//*    this list of conditions and the following disclaimer in the documentation
//*    and/or other materials provided with the distribution.
//*
//* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
//* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
//* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
//* DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
//* FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
//* DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
//* SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
//* CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
//* OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//* OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//*
//**/
//
//#ifndef __AHCI_H__
//#define __AHCI_H__
//
//#include <stdint.h>
//
///* Port Command Bits */
//#define PX_CMD_START   1
//#define PX_CMD_POD     2
//#define PX_CMD_SUD     4
//#define PX_CMD_FRE    (1<<4)
//#define PX_CMD_FR     (1<<14)
//#define PX_CMD_CR     (1<<15)
//#define PX_CMD_ATAPI  (1<<24)
//#define PX_SCTL_NODETECT  0x0
//#define PX_SCTL_DETECT  0x1
//#define PX_SCTL_NOSPEEDLIM 0x0
//#define PX_SCTL_PM_DISABLE  (0x7 << 8)
//#define PX_TFD_ERR  1
//#define PX_TFD_DRQ  (1<<3)
//#define PX_TFD_BUSY (1<<7)
//
//
//#define GHC_BOHC_OOC  (1<<3)
//#define GHC_BOHC_OOS  (1<<1)
//#define GHC_BOHC_SMIE  4
//#define GHC_BOHC_BB    0x10
//#define GHC_BOHC_BOS   1
//
//#define GHC_CAP2_BOH   1
//
//#define FIS_TYPE_REG_H2D  0x27
//#define FIS_TYPE_REG_D2H  0x34
//#define FIS_TYPE_DMA_ACT  0x39
//#define FIS_TYPE_DMA_SETUP  0x41
//#define FIS_TYPE_DATA  0x46
//#define FIS_TYPE_BIST  0x58
//#define FIS_TYPE_PIO_SETUP  0x5F
//#define FIS_TYPE_DEV_BITS   0xA1
//
////!Ata commands
//#define ATA_CMD_IDENTIFY  0xEC
//#define ATA_CMD_READ_DMA  0xC8
//#define ATA_CMD_READ_DMA_EXT 0x25
//#define ATA_CMD_WRITE_DMA  0xCA
//#define ATA_CMD_WRITE_DMA_EXT 0x35
//#define ATA_CMD_PACKET   0xA0
//
//#define FIS_REG_H2D_CTRL_INTERRUPT  (1<<7)
//
//#define HBA_CMD_PRDT_DBC_INTERRUPT (1<<31)
//
//#define SCTL_PORT_DET_INIT   0x1
//#define SCTL_PORT_IPM_NOPART  0x100
//#define SCTL_PORT_IPM_NOSLUM  0x200
//#define SCTL_PORT_IPM_NODSLP  0x400
//#define PX_SCTL_IPM_MASK   0xf << 8
//#define PX_SCTL_IPM_ACTIVE 0x1 << 8
//#define PX_SCTL_IPM_NONE  0x3 << 8
//
//#define HBA_PX_SSTS_DET  0xfULL
//#define HBA_PX_SSTS_DET_INIT  1
//#define HBA_PX_SSTS_DET_PRESENT 3
//
//#define HBA_PX_IS_TFES  (1<<30)
//#define HBA_PX_CMD_ICC  (0xf << 28)
//#define HBA_PX_CMD_ICC_ACTIVE  (1<<28)
//
//#pragma pack(push,1)
//typedef struct _hba_port_ {
//	uint32_t clb;
//	uint32_t clbu;
//	uint32_t fb;
//	uint32_t fbu;
//	uint32_t is;
//	uint32_t ie;
//	uint32_t cmd;
//	uint32_t rsv0;
//	uint32_t tfd;
//	uint32_t sig;
//	uint32_t ssts;
//	uint32_t sctl;
//	uint32_t serr;
//	uint32_t sact;
//	uint32_t ci;
//	uint32_t sntf;
//	uint32_t fbs;
//	uint32_t rsv1[11];
//	uint32_t vendor[4];
//}HBA_PORT;
//#pragma pack(pop)
//
//
//#pragma pack(push,1)
//typedef struct _hba_mem_ {
//	uint32_t cap;
//	uint32_t ghc;
//	uint32_t is;
//	uint32_t pi;
//	uint32_t vs;
//	uint32_t ccc_ctl;
//	uint32_t ccc_pts;
//	uint32_t em_loc;
//	uint32_t em_ctl;
//	uint32_t cap2;
//	uint32_t bohc;
//	uint8_t  rsv[0xA0 - 0x2c];
//	uint8_t  vendor[0x100 - 0xA0];
//	HBA_PORT port[1];
//}HBA_MEM;
//#pragma pack(pop)
//
//#pragma pack(push,1)
//typedef struct _hba_cmd_prdt_ {
//	uint32_t data_base_address;
//	uint32_t dbau;
//	uint32_t reserved;
//	uint32_t data_byte_count : 22;
//	uint32_t rsv1 : 9;
//	uint32_t i : 1;
//}HBA_CMD_PRDT;
//#pragma pack(pop)
//
//#pragma pack(push,1)
//typedef struct _hba_cmd_tbl_ {
//	uint8_t cmd_fis[0x40];
//	uint8_t atapi_cmd[0x10];
//	uint8_t reserved[0x30];
//	HBA_CMD_PRDT prdt[1];
//}HBA_CMD_TABLE;
//#pragma pack(pop)
//
//
//#pragma pack(push,1)
//typedef struct _fis_data_ {
//	uint8_t fis_type;
//	uint8_t pm_port;
//	uint8_t resv1[2];
//	uint32_t data[1];
//}FIS_DATA;
//#pragma pack(pop)
//
//#pragma pack(push,1)
//
//typedef struct _fis_pio_setup_ {
//	/*dword 0*/
//	uint8_t fis_type;
//	uint8_t ctl_byte;
//	uint8_t status;
//	uint8_t error;
//	/*dword 1 */
//	uint8_t lba0;
//	uint8_t lba1;
//	uint8_t lba2;
//	uint8_t device;
//	/*dword 2 */
//	uint8_t lba3;
//	uint8_t lba4;
//	uint8_t lba5;
//	uint8_t rsv2;
//	/*dword 3 */
//	uint8_t countl;
//	uint8_t counth;
//	uint8_t rsv3;
//	uint8_t e_status;
//	/* dword 4 */
//	uint16_t tc;
//	uint8_t rsv4[2];
//}FIS_PIO_SETUP;
//
//typedef struct _fis_reg_h2d_ {
//	/*dword 0*/
//	uint8_t fis_type;
//	uint8_t pmport : 4;
//	uint8_t rsv0 : 3;
//	uint8_t c : 1;
//
//	uint8_t command;
//	uint8_t featurel;
//
//	/*dword 1*/
//	uint8_t lba0;
//	uint8_t lba1;
//	uint8_t lba2;
//	uint8_t device;
//
//	/* dword 2*/
//	uint8_t lba3;
//	uint8_t lba4;
//	uint8_t lba5;
//	uint8_t featureh;
//
//	/* dword 3*/
//	uint8_t countl;
//	uint8_t counth;
//	uint8_t icc;
//	uint8_t control;
//
//	/* dword 4*/
//	uint8_t rsv1[4];
//}FIS_REG_H2D;
//
//typedef struct _fis_reg_d2h_ {
//	/* dword 0 */
//	uint8_t fis_type;
//	uint8_t ctl_byte;
//	uint8_t status;
//	uint8_t error;
//
//	/*dword 1*/
//	uint8_t lba0;
//	uint8_t lba1;
//	uint8_t lba2;
//	uint8_t device;
//
//	/*dword 2*/
//	uint8_t lba3;
//	uint8_t lba4;
//	uint8_t lba5;
//	uint8_t rsv2;
//
//	/*dword 3*/
//	uint8_t countl;
//	uint8_t counth;
//	uint8_t rsv3[2];
//
//	/* dword 4 */
//	uint8_t rsv4[4];
//}FIS_REG_D2H;
//
//
//typedef struct _fis_dma_setup_ {
//	uint8_t fis_type;
//	uint8_t pm_port : 4;
//	uint8_t rsv0 : 1;
//	uint8_t d : 1;
//	uint8_t i : 1;
//	uint8_t a : 1;
//	uint8_t rsved[2];
//	uint64_t dma_buffer_id;
//	uint32_t rsvd;
//	uint32_t dma_buff_offset;
//	uint32_t transfer_count;
//	uint32_t resvd;
//}FIS_DMA_SETUP;
//
//
//typedef struct _hba_fis_ {
//	FIS_DMA_SETUP ds_fis;
//	uint8_t       pad0[4];
//	/* 0x20 */
//	FIS_PIO_SETUP ps_fis;
//	uint8_t       pad1[12];
//	/*0x40*/
//	FIS_REG_D2H   rfis;
//	uint8_t       pad2[4];
//	/*0x58 */
//	uint64_t      sdbfis;
//	/*0x60 */
//	uint8_t       ufis[64];
//	/*0xA0*/
//	uint8_t       rsv[0x100 - 0xA0];
//} HBA_FIS;
//
//#pragma pack(pop)
//
//
//#pragma pack(push,1)
//typedef struct _cmd_list_hdr_ {
//	uint8_t cfl : 5;
//	uint8_t a : 1;
//	uint8_t w : 1;
//	uint8_t p : 1;  //prefetchable
//	uint8_t r : 1;
//	uint8_t b : 1;
//	uint8_t c : 1;
//	uint8_t rsv0 : 1;
//	uint8_t pmp : 4;
//
//	uint16_t prdtl;
//	uint32_t prdbc;
//	uint32_t ctba;
//	uint32_t ctbau;
//	uint32_t reserved[4];
//}HBA_CMD_HEADER;
//#pragma pack(pop)
//
//enum PORT_REGISTERS {
//	Px_CLB = 0,
//	Px_CLBU = 4,
//	Px_FB = 8,
//	Px_FBU = 0xC,
//	Px_IS = 0x10,
//	Px_IE = 0x14,
//	Px_CMD = 0x18,
//	Px_TFD = 0x20,
//	Px_SIG = 0x24,
//	Px_SSTS = 0x28,
//	Px_SCTL = 0x2C,
//	Px_SERR = 0x30,
//	Px_SACT = 0x34,
//	Px_CI = 0x38,
//	Px_SNTF = 0x3C,
//	Px_FBS = 0x40,
//	Px_DEVSLP = 0x44
//};
//
//
///*
//* AuAHCIInitialise -- initialise the ahci interface
//*/
//extern void AuAHCIInitialise();
//#endif