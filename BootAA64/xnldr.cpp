/**
* BSD 2-Clause License
*
* Copyright (c) 2023-2025, Manas Kamal Choudhury
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

#include <Uefi.h>
#include <Guid/DebugImageInfoTable.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Guid/SystemResourceTable.h>
#include "xnldr.h"
#include "video.h"
#include "file.h"
#include "xnout.h"

#include "pe.h"
#include "physm.h"
#include "paging.h"
#include "lowlevel.h"
#include "uart0.h"
#include "vector.h"
#include <Board/imx8mp/imx8mp_uart.h>

/* global variable */
EFI_HANDLE gImageHandle;
EFI_SYSTEM_TABLE* gSystemTable;
EFI_BOOT_SERVICES* gBS;
EFI_RUNTIME_SERVICES* gRS;
EFI_LOADED_IMAGE_PROTOCOL* xnldr2;
EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;
static bool _is_graphics_enabled;

#define ACPI_20_TABLE_GUID                                                                         \
	{0x8868e871, 0xe4f1, 0x11d3, 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}

/*
 * XEGUIDMatch -- compares two given GUID
 * @param guid1 -- GUID one
 * @param guid2 -- GUID two
 */
bool XEGUIDMatch(EFI_GUID guid1, EFI_GUID guid2) {
	bool first_part_good =
		(guid1.Data1 == guid2.Data1 && guid1.Data2 == guid2.Data2 && guid1.Data3 == guid2.Data3);

	if (!first_part_good)
		return false;

	for (int i = 0; i < 8; ++i)
		if (guid1.Data4[i] != guid2.Data4[i])
			return false;

	return true;
}

int XECompareGUID(const EFI_GUID* Guid1, const EFI_GUID* Guid2) {
	if (Guid1->Data1 != Guid2->Data1 || Guid1->Data2 != Guid2->Data2 ||
		Guid1->Data3 != Guid2->Data3)
		return 1;

	for (int i = 0; i < 8; i++) {
		if (Guid1->Data4[i] != Guid2->Data4[i])
			return 1;
	}

	return 0;
}

/*
 * XEInitialiseLib -- initialise the UEFI library
 * @param ImageHandle -- Pointer to EFI_HANDLE
 * @param SystemTable -- Pointer to EFI_SYSTEM_TABLE
 */
EFI_STATUS XEInitialiseLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	gImageHandle = ImageHandle;
	gSystemTable = SystemTable;
	gBS = gSystemTable->BootServices;
	gRS = gSystemTable->RuntimeServices;
	_is_graphics_enabled = false;
	EFI_STATUS Status;
	EFI_GUID loadedimageprot = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL* loadedimage = nullptr;
	Status = gBS->HandleProtocol(gImageHandle, &loadedimageprot, (void**)&loadedimage);
	if (EFI_ERROR(Status)) {
		return Status;
	}
	xnldr2 = loadedimage;
	return EFI_SUCCESS;
}

typedef struct {
	CHAR16 Label[24];
	UINT32 Width;
	UINT32 Height;
	/* Manual -- resolution not offered by the firmware GOP (e.g. 1920x1080
	 * on QEMU edk2-aarch64). Selecting it keeps the current GOP mode for
	 * boot text and records the size as a desktop override for the GPU
	 * driver instead. --axiss */
	BOOLEAN Manual;
} MENU_ITEM;

#define RES_MENU_MAX 12

/* Desktop modes QEMU's firmware GOP never lists; offered as manual
 * override entries after the discovered firmware modes. --axiss */
static const UINT32 kManualRes[][2] = {
	{1280, 720},
	{1366, 768},
	{1600, 900},
	{1920, 1080},
};

static MENU_ITEM MenuItem[RES_MENU_MAX];
static UINTN MenuItemCount = 0;

/* Desktop size override from a manual menu entry (0 = none, use GOP). --axiss */
static UINT16 gDesktopOverrideW = 0;
static UINT16 gDesktopOverrideH = 0;

static void XEUintToChar16(UINT32 value, CHAR16* buf) {
	CHAR16 tmp[12];
	UINTN i = 0;
	if (value == 0) {
		buf[0] = L'0';
		buf[1] = L'\0';
		return;
	}
	while (value > 0) {
		tmp[i++] = (CHAR16)(L'0' + (value % 10));
		value /= 10;
	}
	UINTN j = 0;
	while (i > 0)
		buf[j++] = tmp[--i];
	buf[j] = L'\0';
}

static void XEFormatResolutionLabel(CHAR16* dest, UINT32 w, UINT32 h) {
	CHAR16 wbuf[12], hbuf[12];
	XEUintToChar16(w, wbuf);
	XEUintToChar16(h, hbuf);
	UINTN idx = 0;
	for (UINTN i = 0; wbuf[i]; i++)
		dest[idx++] = wbuf[i];
	dest[idx++] = L'x';
	for (UINTN i = 0; hbuf[i]; i++)
		dest[idx++] = hbuf[i];
	dest[idx] = L'\0';
}

/*
 * XEDiscoverGraphicsModes -- build the resolution menu from every mode the
 * firmware's GOP actually reports, instead of a hardcoded guess list that
 * could offer resolutions the firmware doesn't have (1280x1024, 1920x1080
 * were never in QEMU's edk2-aarch64 GOP mode list, hence permanently
 * "unavailable") while omitting ones it does (800x600) --axiss
 */
static void XEDiscoverGraphicsModes() {
	MenuItemCount = 0;
	EFI_GRAPHICS_OUTPUT_PROTOCOL* graphicsOutput = nullptr;
	EFI_GUID gopGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_STATUS status = gBS->LocateProtocol(&gopGuid, NULL, (VOID**)&graphicsOutput);
	if (EFI_ERROR(status) || !graphicsOutput || !graphicsOutput->Mode)
		return;

	for (UINTN mode = 0; mode < graphicsOutput->Mode->MaxMode && MenuItemCount < RES_MENU_MAX;
		 mode++) {
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
		UINTN infoSize = 0;
		status = graphicsOutput->QueryMode(graphicsOutput, mode, &infoSize, &info);
		if (!EFI_ERROR(status) && info) {
			bool alreadyListed = false;
			for (UINTN i = 0; i < MenuItemCount; i++) {
				if (MenuItem[i].Width == info->HorizontalResolution &&
					MenuItem[i].Height == info->VerticalResolution) {
					alreadyListed = true;
					break;
				}
			}
			if (!alreadyListed) {
				MenuItem[MenuItemCount].Width = info->HorizontalResolution;
				MenuItem[MenuItemCount].Height = info->VerticalResolution;
				MenuItem[MenuItemCount].Manual = FALSE;
				XEFormatResolutionLabel(
					MenuItem[MenuItemCount].Label, info->HorizontalResolution, info->VerticalResolution);
				MenuItemCount++;
			}
		}
		if (info)
			gBS->FreePool(info);
	}
	/* Manual desktop-size entries the firmware GOP does not offer. --axiss */
	for (UINTN m = 0; m < sizeof(kManualRes) / sizeof(kManualRes[0]); m++) {
		if (MenuItemCount >= RES_MENU_MAX)
			break;
		bool alreadyListed = false;
		for (UINTN i = 0; i < MenuItemCount; i++) {
			if (MenuItem[i].Width == kManualRes[m][0] &&
				MenuItem[i].Height == kManualRes[m][1]) {
				alreadyListed = true;
				break;
			}
		}
		if (alreadyListed)
			continue;
		MenuItem[MenuItemCount].Width = kManualRes[m][0];
		MenuItem[MenuItemCount].Height = kManualRes[m][1];
		MenuItem[MenuItemCount].Manual = TRUE;
		XEFormatResolutionLabel(MenuItem[MenuItemCount].Label, kManualRes[m][0],
								kManualRes[m][1]);
		/* Mark manual entries so the firmware-mode ones stay recognizable:
		 * "1920x1080" becomes "1920x1080 *". --axiss */
		UINTN len = 0;
		while (MenuItem[MenuItemCount].Label[len] && len < 21)
			len++;
		MenuItem[MenuItemCount].Label[len++] = L' ';
		MenuItem[MenuItemCount].Label[len++] = L'*';
		MenuItem[MenuItemCount].Label[len] = L'\0';
		MenuItemCount++;
	}
}

/*
 * XEGetScreenResolutionMode -- Provides a selection based menu
 * to user of supported screen resolution by XenevaOS
 * @param SystemTable -- Pointer to EFI SYSTEM TABLE
 */
int XEGetScreenResolutionMode(EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status = EFI_SUCCESS;
	UINTN SelectedIndex = 0;
	EFI_INPUT_KEY Key = {};
	XEDiscoverGraphicsModes();
	BOOLEAN cursorWasVisible = SystemTable->ConOut->Mode->CursorVisible;

	if (MenuItemCount == 0) {
		/* XESetGraphicsMode will retain the firmware mode when nothing was
		 * discovered. There is nothing useful to select here. */
		XEPrintf(const_cast<wchar_t*>(
			L"No listed GOP resolution is available; keeping the firmware mode.\r\n"));
		return 0;
	}

	SystemTable->ConIn->Reset(SystemTable->ConIn, FALSE);
	SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
	SystemTable->ConOut->EnableCursor(SystemTable->ConOut, FALSE);
	while (1) {
		SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);
		XESetTextAttribute(0, EFI_WHITE);
		XEPrintf(const_cast<wchar_t*>(L"XenevaOS loader (XNLDR) 2.0 ARM64 \r\n"));
		XESetTextAttribute(0, EFI_LIGHTGRAY);
		XEPrintf(const_cast<wchar_t*>(L"Copyright (C) Manas Kamal Choudhury 2020-2026 \r\n"));
		XEPrintf(const_cast<wchar_t*>(L"Select a screen resolution with Up/Down, then press Enter:\r\n"));
		XEPrintf(const_cast<wchar_t*>(L"\r\n"));
		for (UINTN i = 0; i < MenuItemCount; i++) {
			if (i == SelectedIndex) {
				SystemTable->ConOut->SetAttribute(SystemTable->ConOut,
												  EFI_WHITE | EFI_BACKGROUND_BLUE);
				XEPrintf(const_cast<wchar_t*>(L"> %-16s\r\n"), MenuItem[i].Label);
			} else {
				SystemTable->ConOut->SetAttribute(SystemTable->ConOut, EFI_LIGHTGRAY);
				XEPrintf(const_cast<wchar_t*>(L"  %-16s\r\n"), MenuItem[i].Label);
			}
		}
		SystemTable->ConOut->SetAttribute(SystemTable->ConOut, EFI_LIGHTGRAY);

		EFI_EVENT keyEvent = SystemTable->ConIn->WaitForKey;
		UINTN eventIndex = 0;
		Status = gBS->WaitForEvent(1, &keyEvent, &eventIndex);
		if (EFI_ERROR(Status))
			break;
		Key = {};
		Status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
		if (EFI_ERROR(Status))
			continue;

		if (Key.ScanCode == SCAN_UP) {
			SelectedIndex = (SelectedIndex + MenuItemCount - 1) % MenuItemCount;
		} else if (Key.ScanCode == SCAN_DOWN) {
			SelectedIndex = (SelectedIndex + 1) % MenuItemCount;
		} else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
			break;
		}
	}

	SystemTable->ConOut->SetAttribute(SystemTable->ConOut, EFI_LIGHTGRAY);
	SystemTable->ConOut->EnableCursor(SystemTable->ConOut, cursorWasVisible);
	return (int)SelectedIndex;
}

/*
 * XESetGraphicsMode -- Set the screen graphics mode based on user selection
 * from the menu
 * @param SystemTable -- Pointer to EFI SYSTEM TABLE
 * @param index -- User selection index
 */
UINTN XESetGraphicsMode(EFI_SYSTEM_TABLE* SystemTable, int index) {
	EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput;
	EFI_GUID gopguid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_STATUS Status;
	UINTN Mode = 0, MaxMode = 0;

	/* dwidth/dheight now come from the menu discovered off the real GOP
	 * mode list (XEDiscoverGraphicsModes), not a hardcoded per-index
	 * switch that could disagree with what the menu actually offered --axiss */
	UINT32 dwidth = 640;
	UINT32 dheight = 480;
	gDesktopOverrideW = 0;
	gDesktopOverrideH = 0;
	if (index >= 0 && (UINTN)index < MenuItemCount) {
		dwidth = MenuItem[index].Width;
		dheight = MenuItem[index].Height;
		if (MenuItem[index].Manual) {
			/* No GOP mode to set: keep the firmware framebuffer as-is
			 * and hand the size to the GPU driver as a desktop
			 * override instead. --axiss */
			gDesktopOverrideW = (UINT16)dwidth;
			gDesktopOverrideH = (UINT16)dheight;
		}
	}
	XEPrintf(
		const_cast<wchar_t*>(L"index %d selected : %dx%d\r\n"), index, (int)dwidth, (int)dheight);

	Status = gBS->LocateProtocol(&gopguid, NULL, (VOID**)&GraphicsOutput);
	if (EFI_ERROR(Status)) {
		XEPrintf(const_cast<wchar_t*>(L"XNLDR 2.0 Failed to locate Graphics Output protocol \r\n"));
		_is_graphics_enabled = false;
		return Status;
	}

	MaxMode = GraphicsOutput->Mode->MaxMode;
	Mode = GraphicsOutput->Mode->Mode;
	bool requestedModeFound = false;
	XEPrintf(const_cast<wchar_t*>(L"Available Screen Resolution:\r\n"));
	for (UINTN i = 0; i < MaxMode; i++) {
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info = nullptr;
		UINTN Size = 0;
		Status = GraphicsOutput->QueryMode(GraphicsOutput, i, &Size, &Info);
		if (EFI_ERROR(Status) || Info == NULL)
			continue;
		if (Info->HorizontalResolution == dwidth && Info->VerticalResolution == dheight) {
			Mode = i;
			requestedModeFound = true;
		}
		gBS->FreePool(Info);
		if (requestedModeFound)
			break;
	}

	Status = GraphicsOutput->SetMode(GraphicsOutput, Mode);
	if (EFI_ERROR(Status)) {
		_is_graphics_enabled = false;
		return Mode;
	}
	gop = GraphicsOutput;
	Status = XEInitialiseGraphics(GraphicsOutput);
	_is_graphics_enabled = !EFI_ERROR(Status);
	return Mode;
}

typedef void (*kentry)(void* ptr);

EFI_GUID FdtTableGuid = {
	0xb1b621d5, 0xf19c, 0x41a5, {0x83, 0x0b, 0xd9, 0x15, 0x2c, 0x69, 0xaa, 0xe0}};

#pragma pack(push, 1)
//! ACPI version 1.0 structures
typedef struct _rsdp_ {
	char signature[8];
	unsigned char checksum;
	char oemId[6];
	unsigned char revision;
	unsigned rsdtAddr;

	unsigned length;
	uint64_t xsdtAddr;
	unsigned char xChecksum;
	unsigned char res[3];
} acpiRsdp;
#pragma pack(pop)

void Char16ToASCII(char* dest, CHAR16* src) {
	while (*src) {
		*dest++ = (char)(*src++ & 0xFF);
	}
	*dest = '\0';
}

void ASCIIToChar16(const char* src, wchar_t* dst) {
	while (*src) {
		*dst++ = (wchar_t)*src++;
	}
	*dst = L'\0';
}

extern "C" void prepare_el2_exit_phase1();
extern "C" void prepare_el2_exit_phase2();

void* kernelBuff;
uint64_t keBuff;

void XEExitEL2() {
	uint64_t spsr_el2 = read_spsr_el2();
	uint64_t sctlr_el1 = read_sctlr_el1();

	prepare_el2_exit_phase1();
	prepare_el2_exit_phase2();
	XEUartInitialize();
	XEUARTPrint("Heyyyyy %x\r\n", 0x1234);
	XEVectorInstall();
	XEUARTPrint("Installed everything \r\n");
	XEUARTPrint("Loading PE Image \r\n");
	XEUARTPrint("KernelBUffer : %x \r\n", kernelBuff);
	void* paddr = (void*)XEPmmngrAllocate();
	void* paddr1 = (void*)XEPmmngrAllocate();
	XEUARTPrint("Physical Address : %x \n", paddr);
	XEUARTPrint("Physical Address2 : %x \r\n", paddr1);
	IMAGE_DOS_HEADER* dos_ = (IMAGE_DOS_HEADER*)keBuff; //0x8000000000;
	XEUARTPrint("DOS Magic : %d \r\n", dos_->e_magic);
}

typedef struct {
	uint32_t magic;
	uint32_t totalSize;
	uint32_t off_dt_struct;
	uint32_t off_dt_strings;
	uint32_t off_mem_rsvmap;
	uint32_t version;
	uint32_t last_comp_version;
	uint32_t boot_cpuid_phys;
	uint32_t size_dt_strings;
	uint32_t size_dt_struct;
} fdt_header_t;

/**
 * @brief AuDTBSwap32 -- swaps 32 bit value
 * @param from -- value to swap
 */
uint32_t AuDTBSwap32(uint32_t from) {
	uint8_t a = from >> 24;
	uint8_t b = from >> 16;
	uint8_t c = from >> 8;
	uint8_t d = from;
	return (d << 24) | (c << 16) | (b << 8) | a;
}

extern void* XEDTBGetHardcodeAddress();
extern void XEPagingInit2();

/*
 * efi_main -- main entry of XNLDR 2.0
 * @param ImageHandle -- System parameter
 * @param SystemTable -- System parameter
 */
extern "C" EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	//	prepare_el2_exit_phase1();
	//	prepare_el2_exit_phase2();
	//	XEUartInitialize();
	//	XEVectorInstall();
	//	XEUARTPrint("Exit el2 \r\n");
	EFI_STATUS Status;
	Status = XEInitialiseLib(ImageHandle, SystemTable);
	XEUARTPrint("Library initialized \r\n");
	XEClearScreen();
	XEBootInfo bootinfo;
	/* The resolution menu shows by default. Packaging --no-boot-menu drops
	 * a NOMENU marker on the ESP, and headless/egl/xr runs always skip --
	 * nothing interactive can answer there. --axiss */
	int index = 0;
	if (!XEFileExists(ImageHandle, (CHAR16*)L"\\NOMENU"))
		index = XEGetScreenResolutionMode(SystemTable);
	/* Set the graphics resolution based on user selection */
	UINTN Mode = XESetGraphicsMode(SystemTable, index);
	XEGuiPrint("XenevaOS Loader 2.0 (XNLDR) ARM64\n");
	XEGuiPrint("Copyright (C) Manas Kamal Choudhury 2020-2025\n");

	XEGuiPrint("Loading system files.. please wait !! \n");

	uint64_t adddr = 0x50000000;
	EFI_CONFIGURATION_TABLE* configuration_tables = SystemTable->ConfigurationTable;

	/* load all important files */
	XEFile* krnl = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\EFI\\XENEVA\\xnkrnl.exe");
	uint8_t* alignedKBuf = (uint8_t*)krnl->kBuffer;
	kernelBuff = krnl->kBuffer;
	keBuff = (uint64_t)krnl->kBuffer;
	IMAGE_DOS_HEADER* dosHeader = (IMAGE_DOS_HEADER*)alignedKBuf;
	IMAGE_NT_HEADERS* ntHeader = (IMAGE_NT_HEADERS*)(alignedKBuf + dosHeader->e_lfanew);
	bool isKernelValid = false;
	if (dosHeader->e_magic == 0x5A4D)
		isKernelValid = true;

	char ps[16];
	sztoa(dosHeader->e_magic, ps, 16);
	wchar_t ps16[16];
	ASCIIToChar16(ps, ps16);
	XEPrintf(const_cast<wchar_t*>(ps16));
	XEPrintf(const_cast<wchar_t*>(L"\r\n"));

	if (!isKernelValid) {
		XEGuiPrint("XNLDR: kernel image corrupted \n");
		XEPrintf(const_cast<wchar_t*>(L"Kernel image is corrupted \r\n"));
		for (;;)
			;
	}

#ifdef __TARGET_BOARD_QEMU_VIRT__
	XEFile* initrd = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\initrd2.img");
#else
	XEFile* initrd = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\initrd.img");
#endif

	/**
	 *-------------------------------------------------------------------
	 * Get the address of ACPI Table
	 *-------------------------------------------------------------------
	 */
	void* xdsp_address = NULL;
	static EFI_GUID acpi_guid = ACPI_20_TABLE_GUID;
	for (unsigned i = 0; i < gSystemTable->NumberOfTableEntries; ++i) {
		if (XEGUIDMatch(acpi_guid, configuration_tables[i].VendorGuid)) {
			xdsp_address = configuration_tables[i].VendorTable;
		}
	}

	void* fdt_address = NULL;
	for (unsigned i = 0; i < SystemTable->NumberOfTableEntries; i++) {
		if (XEGUIDMatch(configuration_tables[i].VendorGuid, FdtTableGuid)) {
			char magi[16];
			sztoa((size_t)SystemTable->ConfigurationTable[i].VendorTable, magi, 16);
			wchar_t mg_16[16];
			ASCIIToChar16(magi, mg_16);
			XEPrintf(const_cast<wchar_t*>(L"FDT Table pointer -- "));
			XEPrintf(const_cast<wchar_t*>(mg_16));
			XEPrintf(const_cast<wchar_t*>(L"\r\n"));
			fdt_address = SystemTable->ConfigurationTable[i].VendorTable;
		}
	}

	bool _need_fdt_hardcode = false;
	fdt_header_t* fd_ = (fdt_header_t*)fdt_address;
	if (fd_) {
		XEPrintf(const_cast<wchar_t*>(L"FDT Address gathered from EFI_CONFIGURATION_TABLES \r\n"));
		if (AuDTBSwap32(fd_->magic) == 0xd00dfeed) {
			XEPrintf(const_cast<wchar_t*>(L"Yess DTB was correct \r\n"));
		} else
			_need_fdt_hardcode = true;
	}

	if (_need_fdt_hardcode) {
		fdt_address = XEDTBGetHardcodeAddress();
		XEPrintf(const_cast<wchar_t*>(L"Going through hardcoded DTB address \r\n"));
		fdt_header_t* fdt = (fdt_header_t*)fdt_address;
		char magic[16];
		sztoa(read_ttbr0_el2(), magic, 16);
		wchar_t mg16[16];
		ASCIIToChar16(magic, mg16);
		XEPrintf(const_cast<wchar_t*>(mg16));
		XEPrintf(const_cast<wchar_t*>(L"\r\n"));
	}

	const size_t EARLY_PAGE_STACK_SIZE = 1024 * 1024;
	EFI_PHYSICAL_ADDRESS earlyPhyPageStack = 0;
	if ((SystemTable->BootServices->AllocatePages(AllocateAnyPages,
												  EfiLoaderData,
												  EARLY_PAGE_STACK_SIZE / EFI_PAGE_SIZE,
												  (EFI_PHYSICAL_ADDRESS*)&earlyPhyPageStack)) !=
		EFI_SUCCESS) {
		XEGuiPrint("Early Page Stack: allocation failed.....\n");
	}

	struct EfiMemoryMap map;
	EFI_MEMORY_DESCRIPTOR* desc_ptr = nullptr;
	map.MemMapSize = 0;
	map.MapKey = map.DescriptorSize = map.DescriptorVersion = 0;
	map.memmap = 0;
	Status = gSystemTable->BootServices->GetMemoryMap(
		&map.MemMapSize, map.memmap, &map.MapKey, &map.DescriptorSize, &map.DescriptorVersion);
	if (Status == EFI_BUFFER_TOO_SMALL) {
		/* Expected behavior: UEFI returns EFI_BUFFER_TOO_SMALL when querying the required buffer size. Muting to avoid console spam. */
		// XEGuiPrint("Failed memory map! Buffer to small \n");
		// XEPrintf(const_cast<wchar_t*>(L"Failed memory map ! Buffer to small \r\n"));
		// XEGuiPrint("Required buffer -> %d bytes\n", map.MemMapSize);
		// XEPrintf(const_cast<wchar_t*>(L"Required buffer size : %d \r\n"), map.MemMapSize);
	} else if (Status == EFI_INVALID_PARAMETER) {
		XEGuiPrint("EFI_Memory_Map failed!!, invalid parameter \n");
		XEPrintf(const_cast<wchar_t*>(L"EFI_Memory_Map failed!! invalid parameter \r\n"));
	} else if (Status != EFI_SUCCESS) {
		XEGuiPrint("Memory Map Failed \n");
		XEPrintf(const_cast<wchar_t*>(L"Memory Map failed \r\n"));
	}

	//give a nice bit of room to spare
	map.MemMapSize += 2 * map.DescriptorSize; //sizeof(EFI_MEMORY_DESCRIPTOR);
	/* DEBUG: direct UART write to trace bootloader progress */
	{
		volatile unsigned int* dbg_uart = (volatile unsigned int*)0x09000000;
		while (*(dbg_uart + 6) & (1 << 5))
			;
		*dbg_uart = 'm';
		while (*(dbg_uart + 6) & (1 << 5))
			;
		*dbg_uart = 'm';
		while (*(dbg_uart + 6) & (1 << 5))
			;
		*dbg_uart = 'p';
		while (*(dbg_uart + 6) & (1 << 5))
			;
		*dbg_uart = '\r';
		while (*(dbg_uart + 6) & (1 << 5))
			;
		*dbg_uart = '\n';
	}
	char magic[16];
	sztoa(map.DescriptorSize, magic, 16);
	wchar_t mg16[16];
	ASCIIToChar16(magic, mg16);
	XEPrintf(const_cast<wchar_t*>(mg16));
	XEPrintf(const_cast<wchar_t*>(L"\r\n"));
	if (map.MemMapSize == 0) {
		map.MemMapSize = 1024;
	}
	VOID* Buffer;
	Status = SystemTable->BootServices->AllocatePool(EfiLoaderData,
		map.MemMapSize + 16 * map.DescriptorSize, &Buffer);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to allocate pool memory \r\n");
		for (;;);
	}
	map.memmap = (EFI_MEMORY_DESCRIPTOR*)Buffer;

	/* GetMemoryMap + ExitBootServices with retry (UEFI spec recommended pattern).
	   EBS can fail with EFI_INVALID_PARAMETER if the memory map changed between
	   GetMemoryMap and ExitBootServices. The retry loop re-fetches the map. */
	for (int retries = 0; retries < 16; ++retries) {
		Status = gSystemTable->BootServices->GetMemoryMap(
			&map.MemMapSize, map.memmap, &map.MapKey,
			&map.DescriptorSize, &map.DescriptorVersion);
		if (Status == EFI_BUFFER_TOO_SMALL) {
			/* Buffer was too small — reallocate with the updated size, plus the
			   same headroom as the initial allocation. Without slack here, the
			   FreePool/AllocatePool pair below can itself perturb the memory map
			   (splitting/coalescing pool descriptors), so the very next
			   GetMemoryMap can come back EFI_BUFFER_TOO_SMALL again — a livelock
			   that can burn through all the retries and never reach
			   ExitBootServices at all. */
			SystemTable->BootServices->FreePool(Buffer);
			Status = SystemTable->BootServices->AllocatePool(
				EfiLoaderData, map.MemMapSize + 16 * map.DescriptorSize, &Buffer);
			if (EFI_ERROR(Status)) {
				XEGuiPrint("Failed to reallocate pool memory \r\n");
				for (;;);
			}
			map.memmap = (EFI_MEMORY_DESCRIPTOR*)Buffer;
			continue;
		}
		if (EFI_ERROR(Status)) {
			XEGuiPrint("Failed to retrieve memory map \n");
			continue;
		}

		Status = SystemTable->BootServices->ExitBootServices(
			ImageHandle, map.MapKey);
		if (Status == EFI_SUCCESS)
			break;
		/* EFI_INVALID_PARAMETER → map was stale, retry with fresh map */
	}
	if (Status != EFI_SUCCESS) {
		XEGuiPrint("Exit Boot Service Failed \n");
		XEPrintf(const_cast<wchar_t*>(L"Exit Boot Services failed\r\n"));
		for (;;)
			;
	}

	/**
	 * @brief after exit_boot_service, no serial printing function, available from 
	 * efi tables, we need to set up our own
	 */
	XEUartInitialize();

	{
		volatile unsigned int* dbg_uart3 = (volatile unsigned int*)0x09000000;
		while (*(dbg_uart3 + 6) & (1 << 5))
			;
		*dbg_uart3 = 'X';
	}
	XEGuiPrint("Exit bootloader successfull %x\r\n", 0x1000);
	XEInitialisePmmngr(map, (void*)earlyPhyPageStack, EARLY_PAGE_STACK_SIZE);
    XEGuiPrint("Initialized PMMngr \r\n");
	XEPagingInitialize();

	if (_getCurrentEL() != 1) {
		prepare_el2_exit_phase1();
		prepare_el2_exit_phase2();
	}
	/* install our vector table unconditionally */
	XEVectorInstall();

	uint64_t sctlr = read_sctlr_el1();

	if (sctlr & (1UL << 0))
		XEUARTPrint("MMU is turned ON \r\n");
	else
		XEUARTPrint("MMU is disabled \r\n");

#if !__TARGET_BOARD_QEMU_VIRT__
	XEPagingInit2();
#endif

	//XEUARTPrint("MMU Enabled \r\n");

	IMAGE_DOS_HEADER* dos_ = (IMAGE_DOS_HEADER*)krnl->kBuffer;

	//XEUARTPrint("DOS Magic : %x \r\n", dos_->e_magic);

	{
		volatile unsigned int* dbg_uart3 = (volatile unsigned int*)0x09000000;
		while (*(dbg_uart3 + 6) & (1 << 5))
			;
		*dbg_uart3 = 'P';
	}
	XEPELoadImage(krnl->kBuffer);

	for (int i = 0; i <= 0x100000 / PAGESIZE; i++) {
		XEPagingMap(0xFFFFA00000000000 + i * PAGESIZE, XEPmmngrAllocate());
	}
	XEPagingInstallPhysicalDirectMap();

	/*
	 * Changes are made according to RPI_EFI
	 * kernel is mapped to 0x8000000000 due to there's
	 * no EL2_TTBR1 for higher half mapping, and also need
	 * to drop to EL1 before entering Kernel
	 */
	bootinfo.boot_type = BOOT_UEFI_ARM64;
	bootinfo.allocated_stack = XEGetAlstackptr();
	bootinfo.reserved_mem_count = XEReserveMemCount();
	bootinfo.map = map.memmap;
	bootinfo.descriptor_size = map.DescriptorSize;
	bootinfo.mem_map_size = map.MemMapSize;
	bootinfo.physical_direct_map_base = 0xFFFF800000000000ULL;
	bootinfo.physical_direct_map_size = 512ULL << 30;
	bootinfo.graphics_framebuffer = XEGetFramebuffer();
	bootinfo.X_Resolution = XEGetScreenWidth();
	bootinfo.Y_Resolution = XEGetScreenHeight();
	/* Real GOP geometry above; manual menu choice (if any) travels
	 * separately so the early console never runs off the framebuffer. --axiss */
	bootinfo.DesktopOverrideWidth = gDesktopOverrideW;
	bootinfo.DesktopOverrideHeight = gDesktopOverrideH;
	bootinfo.fb_size = XEGetFramebufferSz();
	bootinfo.pixels_per_line = XEGetPixelsPerLine();
	bootinfo.redmask = XEGetRedMask();
	bootinfo.greenmask = XEGetGreenMask();
	bootinfo.bluemask = XEGetBlueMask();
	bootinfo.resvmask = XEGetResvMask();
	bootinfo.acpi_table_pointer = xdsp_address;
	bootinfo.kernel_size = krnl->FileSize;
	bootinfo.printf_gui = XEGuiPrint;
	bootinfo.psf_font_data = 0;
	bootinfo.driver_entry1 = (uint8_t*)initrd->kBuffer;
	bootinfo.driver_entry2 = 0;
	bootinfo.driver_entry3 = 0; // (uint8_t*)xhciAddr;// usbAddr;
	bootinfo.driver_entry4 = 0;
	bootinfo.driver_entry5 = 0;
	bootinfo.driver_entry6 = 0;
	bootinfo.apcode = fdt_address;
	bootinfo.hid = initrd->FileSize;
	bootinfo.uid = 0;
	bootinfo.cid = 0;

	uint64_t image_base = ntHeader->OptionalHeader.ImageBase;
	if (image_base == 0) {
		image_base = 0xFFFFC00000000000;
	}

	VOID* entry = (VOID*)(image_base + ntHeader->OptionalHeader.AddressOfEntryPoint);
	{
		volatile unsigned int* dbg_uart4 = (volatile unsigned int*)0x09000000;
		while (*(dbg_uart4 + 6) & (1 << 5))
			;
		*dbg_uart4 = 'C';
	}
	XEGuiPrint("entry addr : %x bootinfo : %x \r\n", entry, &bootinfo);
	callKernel(&bootinfo, 0xFFFFA00000000000, 0x100000, entry);
	while (1)
		;
}

bool _is_GraphicsEnabled() {
	return _is_graphics_enabled;
}
