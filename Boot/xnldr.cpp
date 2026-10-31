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

#include "xnout.h"
#include "xnldr.h"
#include "video.h"
#include "file.h"
#include "clib.h"
#include "pe.h"
#include "physm.h"
#include "paging.h"
#include "lowlevel.h"

/* global variable */
EFI_HANDLE   gImageHandle;
EFI_SYSTEM_TABLE* gSystemTable;

// TODO : Look into this quirk between GNU-EFI and our build process on MSVC
#ifdef _MSC_VER
EFI_BOOT_SERVICES* gBS;
#endif

EFI_RUNTIME_SERVICES* gRS;
EFI_LOADED_IMAGE_PROTOCOL* xnldr2;
EFI_GRAPHICS_OUTPUT_PROTOCOL* gop;

/*
 * XEGUIDMatch -- compares two given GUID
 * @param guid1 -- GUID one
 * @param guid2 -- GUID two
 */
bool XEGUIDMatch(EFI_GUID guid1, EFI_GUID guid2) {
	bool first_part_good = (guid1.Data1 == guid2.Data1 && guid1.Data2 == guid2.Data2 &&
		guid1.Data3 == guid2.Data3);

	if (!first_part_good) return false;

	for (int i = 0; i < 8; ++i) 
		if (guid1.Data4[i] != guid2.Data4[i]) return false;

	return true;
}

/*
 * XEInitialiseLib -- initialise the UEFI library
 * @param ImageHandle -- Pointer to EFI_HANDLE
 * @param SystemTable -- Pointer to EFI_SYSTEM_TABLE
 */
EFI_STATUS XEInitialiseLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	gImageHandle = ImageHandle;
	gSystemTable = SystemTable;
#ifdef _MSC_VER
	gBS = gSystemTable->BootServices;
#endif
	gRS = gSystemTable->RuntimeServices;

	EFI_STATUS Status;
	EFI_GUID loadedimageprot = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE_PROTOCOL* loadedimage = nullptr;
	if ((Status = gBS->HandleProtocol(gImageHandle, &loadedimageprot, (void**)&loadedimage)) != EFI_SUCCESS)
	{
		return Status;
	}
	xnldr2 = loadedimage;
	return EFI_SUCCESS;
}


typedef struct {
	CHAR16* Label;
}MENU_ITEM;

MENU_ITEM MenuItem[] = {
	{(CHAR16*)u"640x480"},
	{(CHAR16*)u"1024x768"},
	{(CHAR16*)u"1280x1024"},
	{(CHAR16*)u"1920x1080"}
};

#define MENU_SIZE (sizeof(MenuItem)/sizeof(MenuItem[0]))

/*
 * XEGetScreenResolutionMode -- Provides a selection based menu
 * to user of supported screen resolution by XenevaOS
 * @param SystemTable -- Pointer to EFI SYSTEM TABLE
 */
int XEGetScreenResolutionMode(EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status;
	UINTN SelectedIndex = 0;
	EFI_INPUT_KEY Key;
	while (1) {
		SystemTable->ConOut->SetCursorPosition(SystemTable->ConOut, 0, 0);
		XESetTextAttribute(0, EFI_WHITE);
		XEPrintf(u"XenevaOS loader (XNLDR) 2.0 \r\n");
		XESetTextAttribute(0, EFI_LIGHTGRAY);
		XEPrintf(u"Copyright (C) Manas Kamal Choudhury 2020-2025 \r\n");
		XEPrintf(u"Select a screen resolution:\r\n");
		XEPrintf(u"\r\n");
		for (UINTN i = 0; i < MENU_SIZE; i++) {
			if (i == SelectedIndex) {
				SystemTable->ConOut->SetAttribute(SystemTable->ConOut, EFI_WHITE | EFI_BACKGROUND_BLUE);
			}
			else {
				SystemTable->ConOut->SetAttribute(SystemTable->ConOut, EFI_LIGHTGRAY);
			}
			XEPrintf(u"%s\r\n", MenuItem[i].Label);
		}


		do {
			Status = SystemTable->ConIn->ReadKeyStroke(SystemTable->ConIn, &Key);
		} while (Status == EFI_NOT_READY);

		if (Key.ScanCode == SCAN_UP) {
			if (SelectedIndex > 0)
				SelectedIndex--;
		}
		else if (Key.ScanCode == SCAN_DOWN) {
			if (SelectedIndex < MENU_SIZE - 1) {
				SelectedIndex++;
			}
		}
		else if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
			break;
		}

		SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
	}

	return SelectedIndex;
}

/*
 * XESetGraphicsMode -- Set the screen graphics mode based on user selection
 * from the menu
 * @param SystemTable -- Pointer to EFI SYSTEM TABLE
 * @param index -- User selection index
 */
UINTN XESetGraphicsMode( [[maybe_unused]] EFI_SYSTEM_TABLE* SystemTable, int index) {
	EFI_GRAPHICS_OUTPUT_PROTOCOL* GraphicsOutput;
	EFI_GUID gopguid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_STATUS Status;
	// TODO: Are these default values correct?
	UINTN Mode, MaxMode = 0;

	UINT32 dwidth = 0;
	UINT32 dheight = 0;

	switch (index) {
	case 1:
		dwidth = 1024, dheight = 768;
		break;
	case 2:
		dwidth = 1280, dheight = 1024;
		break;
	case 3:
		dwidth = 1920, dheight = 1080;
		break;
	default:
		dwidth = 640, dheight = 480;
		break;
	}

	Status = gBS->LocateProtocol(&gopguid, NULL, (VOID**)&GraphicsOutput);
	if (EFI_ERROR(Status)) {
		XEPrintf(u"XNLDR 2.0 Failed to locate Graphics Output protocol \r\n");
		return Status;
	}

	MaxMode = GraphicsOutput->Mode->MaxMode;
	XEPrintf(u"Available Screen Resolution:\r\n");
	for (UINTN i = 0; i < MaxMode; i++) {
		EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
		UINTN Size;
		Status = GraphicsOutput->QueryMode(GraphicsOutput, i, &Size, &Info);
		if (Info->HorizontalResolution == dwidth && Info->VerticalResolution == dheight)
			Mode = i;
	}

	Status = GraphicsOutput->SetMode(GraphicsOutput, Mode);
	gop = GraphicsOutput;
	Status = XEInitialiseGraphics(GraphicsOutput);
	return Mode;
}

EFI_STATUS efi_main_handler(EFI_HANDLE, EFI_SYSTEM_TABLE*);

extern "C" EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable)
{
	return efi_main_handler(ImageHandle, SystemTable);

}

/*
 * efi_main -- main entry of XNLDR 2.0
 * @param ImageHandle -- System parameter
 * @param SystemTable -- System parameter
 */
EFI_STATUS efi_main_handler(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
	EFI_STATUS Status;
	Status = XEInitialiseLib(ImageHandle, SystemTable);
	XEClearScreen();

	XEBootInfo bootinfo;

	/* Get user graphics resolution choice*/
	int index = XEGetScreenResolutionMode(SystemTable);
	/* Set the graphics resolution based on user selection */
	[[maybe_unused]] UINTN Mode = XESetGraphicsMode(SystemTable, index);
	XEGuiPrint("XenevaOS Loader 2.0 (XNLDR) \n");
	XEGuiPrint("Copyright (C) Manas Kamal Choudhury 2020-2025\n");

	/* load all important files */
	XEFile* krnl = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\EFI\\XENEVA\\xnkrnl.exe");
	XEFile* kfont = XEOpenAndReadFile(ImageHandle,(CHAR16*)L"\\EFI\\XENEVA\\font.psf");
	XEFile* kApCode = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\EFI\\XENEVA\\ap.bin");
	XEFile* kAhci = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\initrd.img");
	//XEFile* kNvme = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\nvme.dll");
	//XEFile* kXHCI = XEOpenAndReadFile(ImageHandle, (CHAR16*)L"\\xhci.dll");
	

	uint8_t* allignedKernelBuffer = (uint8_t*)krnl->kBuffer;
	IMAGE_DOS_HEADER* dos_header = (IMAGE_DOS_HEADER*)allignedKernelBuffer;
	IMAGE_NT_HEADERS* nt_header = (IMAGE_NT_HEADERS*)(allignedKernelBuffer + dos_header->e_lfanew);
	VOID* entry = (VOID*)(nt_header->OptionalHeader.ImageBase + nt_header->OptionalHeader.AddressOfEntryPoint);

	XEGuiPrint("System files loaded successfully \n");
	XEGuiPrint("DOS Signature : %x , NTHeader : %x \r\n", dos_header->e_magic, nt_header->Signature);
	EFI_CONFIGURATION_TABLE* configuration_tables = gSystemTable->ConfigurationTable;

	/**
	 *-------------------------------------------------------------------
	 * Get the address of ACPI Table
	 *-------------------------------------------------------------------
	 */
	void* xdsp_address = NULL;
#ifdef __MSC_VER
	static EFI_GUID acpi_guid = EFI_ACPI_20_TABLE_GUID;
#else
	static EFI_GUID acpi_guid = ACPI_20_TABLE_GUID; 
#endif
	for (unsigned i = 0; i < gSystemTable->NumberOfTableEntries; ++i) {
		if (XEGUIDMatch(acpi_guid, configuration_tables[i].VendorGuid)) {
			xdsp_address = configuration_tables[i].VendorTable;
		}
	}


	const size_t EARLY_PAGE_STACK_SIZE = 1024 * 1024;
	EFI_PHYSICAL_ADDRESS earlyPhyPageStack = 0;
	if (
		(Status = SystemTable->BootServices->AllocatePages(
			AllocateAnyPages, EfiLoaderData, 
			EARLY_PAGE_STACK_SIZE / EFI_PAGE_SIZE, (EFI_PHYSICAL_ADDRESS*)&earlyPhyPageStack)) != EFI_SUCCESS) 
	{
		XEGuiPrint("Early Page Stack: allocation failed, %x", Status);
		for(;;);
	}

	/*Status = gSystemTable->BootServices->SetWatchdogTimer(0, 0, 0, 0);
	if (Status != EFI_SUCCESS) 
		XEGuiPrint("Failed to set watchdog time \n");*/


	struct EfiMemoryMap map;
	map.MemMapSize = 0;
	map.MapKey = map.DescriptorSize = map.DescriptorVersion = 0;
	map.memmap = 0;
	Status = gSystemTable->BootServices->GetMemoryMap(&map.MemMapSize, nullptr, &map.MapKey, &map.DescriptorSize, &map.DescriptorVersion);
	if (Status == EFI_BUFFER_TOO_SMALL) {
		XEGuiPrint("Failed memory map! Buffer to small \n");
		XEGuiPrint("Required buffer -> %d bytes\n", map.MemMapSize);
	}
	else if (Status == EFI_INVALID_PARAMETER) 
		XEGuiPrint("EFI_Memory_Map failed!!, invalid parameter \n");
	else if (Status != EFI_SUCCESS) 
		XEGuiPrint("Memory Map Failed \n");
	

	//give a nice bit of room to spare
	map.MemMapSize += 16 * map.DescriptorSize; //sizeof(EFI_MEMORY_DESCRIPTOR);

	XEGuiPrint("Memory Map size -> %d \n", map.MemMapSize);
	map.memmap = (EFI_MEMORY_DESCRIPTOR*)XEAllocatePool(map.MemMapSize);

	Status = gSystemTable->BootServices->GetMemoryMap(&map.MemMapSize, map.memmap, &map.MapKey, &map.DescriptorSize, &map.DescriptorVersion);
	if (Status != EFI_SUCCESS) 
		XEGuiPrint("Failed to retrieve memory map \n");

	//XEGraphicsClearScreen(gop);

	Status = SystemTable->BootServices->ExitBootServices(ImageHandle, map.MapKey);
	if (Status != EFI_SUCCESS) {
		XEGuiPrint("Exit Boot Service Failed \n");
		for (;;);
	}

	/* Initialise the physical memory manager */
	XEInitialisePmmngr(map, (void*)earlyPhyPageStack, EARLY_PAGE_STACK_SIZE);

	/* initilise paging */
	XEInitialisePaging();

	[[maybe_unused]] void* base = XEPELoadImage(krnl->kBuffer);
	[[maybe_unused]] XEImageEntry kentry = (XEImageEntry)XEPEGetEntryPoint(krnl->kBuffer);

	//uint64_t ahciAddr = XEPELoadDLLImage(kAhci->kBuffer);
	//uint64_t nvmeAddr = XEPELoadDLLImage(kNvme->kBuffer);
	//uint64_t xhciAddr = XEPELoadDLLImage(kXHCI->kBuffer);
	
	void* stackaddr = (void*)0xFFFFA00000000000;
	void* idtaddr = (void*)0xFFFFD80000000000;

	XEPagingMap(stackaddr, PADDR_T_MAX, 0x100000, 0);
	memset(stackaddr, 0, 0x100000);
	XEGuiPrint("Stack mapped \n");
  
	XEPagingMap(idtaddr, PADDR_T_MAX, 0x1000, 0); //for idt

	XEGuiPrint("Loading the kernel \n");
	//Memory Related
	bootinfo.boot_type = 1;
	bootinfo.allocated_mem = XEGetAlstackptr();
	bootinfo.reserved_mem_count = XEReserveMemCount();
	bootinfo.map = map.memmap;
	bootinfo.descriptor_size = map.DescriptorSize;
	bootinfo.mem_map_size = map.MemMapSize;
	bootinfo.graphics_framebuffer = XEGetFramebuffer();
	bootinfo.X_Resolution = XEGetScreenWidth();
	bootinfo.Y_Resolution = XEGetScreenHeight();
	bootinfo.fb_size = XEGetFramebufferSz();
	bootinfo.pixels_per_line = XEGetPixelsPerLine();
	bootinfo.redmask = XEGetRedMask();
	bootinfo.greenmask = XEGetGreenMask();
	bootinfo.bluemask = XEGetBlueMask();
	bootinfo.resvmask = XEGetResvMask();
	bootinfo.acpi_table_pointer = xdsp_address;
	bootinfo.kernel_size = krnl->FileSize;
	bootinfo.printf_gui = XEGuiPrint;
	bootinfo.font_binary_address = (uint8_t*)kfont->kBuffer;
	bootinfo.driver_entry1 = (uint8_t*)kAhci->kBuffer;
	bootinfo.driver_entry2 = 0; // (uint8_t*)nvmeAddr;
	bootinfo.driver_entry3 = 0; // (uint8_t*)xhciAddr;// usbAddr;
	bootinfo.driver_entry4 = 0;
	bootinfo.driver_entry5 = 0;
	bootinfo.driver_entry6 = 0;
	bootinfo.ap_code = kApCode->kBuffer;
	bootinfo.hid =kAhci->FileSize;
	bootinfo.uid =0;
	bootinfo.cid =0;

	XEGuiPrint("Jumping to kernel \n");
	call_kernel(&bootinfo, entry, stackaddr, 0x100000);  //This functions actually does the installation of stack, not for calling kernel
	for (;;);
	return EFI_SUCCESS;
}
