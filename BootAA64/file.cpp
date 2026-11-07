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

#include "file.h"
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadFile.h>
#include <Protocol/LoadedImage.h>
#include <Guid/FileInfo.h>

EFI_GUID FileSystemProtocol = {
	0x964E5B22, 0x6459, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

EFI_GUID LoadFileProtocol = {
	0x56EC3091, 0x954C, 0x11D2, {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

EFI_GUID GenericFileInfo = {
	0x9576E92, 0x6D3F, 0x11D2, {0x8E, 0x39, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}};

#define EFI_FILE_SYSTEM_INFO_ID                                                                    \
	{0x9576e93, 0x6d3f, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b}}

/*
 * XEOpenAndReadFile -- open and reads a file
 * @param ImageHandle -- Image handle passed by EFI firmware
 * @param Filename -- name and path of the file
 */
XEFile* XEOpenAndReadFile(EFI_HANDLE ImageHandle, CHAR16* Filename) {
	EFI_STATUS Status;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFileSystem = nullptr;
	EFI_GUID loadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE* loadedImage = nullptr;
	EFI_FILE_PROTOCOL* Root = nullptr;
	EFI_FILE_PROTOCOL* File = nullptr;
	EFI_FILE_INFO* FileInfo = nullptr;
	UINTN FileInfoSize = 0;
	UINTN FileSize;
	UINTN PageCount = 0;
	EFI_PHYSICAL_ADDRESS Buffer;
	XEFile* xefile;

	EFI_GUID sfsprotocol = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

	Status = gBS->HandleProtocol(ImageHandle, &loadedImageProtocol, (void**)&loadedImage);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to locate image handle \n");
		return 0;
	}

	Status =
		gBS->HandleProtocol(loadedImage->DeviceHandle, &sfsprotocol, (VOID**)&SimpleFileSystem);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to locate file system protocol \n");
		return 0;
	}

	Status = SimpleFileSystem->OpenVolume(SimpleFileSystem, &Root);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to open the root directory \n");
		return 0;
	}

	Status = Root->Open(Root, &File, Filename, EFI_FILE_MODE_READ, 0);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to open file \n");
		Root->Close(Root);
		return 0;
	}

	Status = File->GetInfo(File, &GenericFileInfo, &FileInfoSize, NULL);
	if (Status != EFI_BUFFER_TOO_SMALL || FileInfoSize == 0) {
		XEGuiPrint("Failed to query file metadata size \n");
		File->Close(File);
		Root->Close(Root);
		return 0;
	}
	Status = gBS->AllocatePool(EfiBootServicesData, FileInfoSize, (VOID**)&FileInfo);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to allocate buffer for file metadata \n");
		File->Close(File);
		Root->Close(Root);
		return 0;
	}

	Status = File->GetInfo(File, &GenericFileInfo, &FileInfoSize, FileInfo);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to get file metadata \n");
		gBS->FreePool(FileInfo);
		File->Close(File);
		Root->Close(Root);
		return 0;
	}
	FileSize = FileInfo->FileSize;
	gBS->FreePool(FileInfo);
	if (FileSize == 0) {
		XEGuiPrint("Refusing to load an empty file \n");
		File->Close(File);
		Root->Close(Root);
		return 0;
	}

	PageCount = (FileSize + 0xFFF) / 0x1000;
	Status = gBS->AllocatePages(AllocateAnyPages, EfiLoaderData, PageCount, &Buffer);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to allocate buffer for file content \n");
		File->Close(File);
		Root->Close(Root);
		return 0;
	}

	void* readBuff = (void*)Buffer;
	UINTN bytesRead = FileSize;
	Status = File->Read(File, &bytesRead, readBuff);
	if (EFI_ERROR(Status) || bytesRead != FileSize) {
		XEGuiPrint("Failed to read file \n");
		gBS->FreePages(Buffer, PageCount);
		File->Close(File);
		Root->Close(Root);
		return 0;
	}

	xefile = (XEFile*)XEAllocatePool(sizeof(XEFile));
	if (!xefile) {
		XEGuiPrint("Failed to allocate file data structure \n");
		gBS->FreePages(Buffer, PageCount);
		File->Close(File);
		Root->Close(Root);
		return 0;
	}
	memset(xefile, 0, sizeof(XEFile));
	xefile->kBuffer = (void*)Buffer;
	xefile->FileSize = FileSize;
	xefile->PageCount = PageCount;
	File->Close(File);
	Root->Close(Root);

	return xefile;
}

/*
 * XEFileExists -- probe for a file without reading it
 * @param ImageHandle -- Image handle passed by EFI firmware
 * @param Filename -- name and path of the file
 * @return true when the file can be opened for reading
 */
bool XEFileExists(EFI_HANDLE ImageHandle, CHAR16* Filename) {
	EFI_STATUS Status;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFileSystem = nullptr;
	EFI_GUID loadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE* loadedImage = nullptr;
	EFI_FILE_PROTOCOL* Root = nullptr;
	EFI_FILE_PROTOCOL* File = nullptr;

	EFI_GUID sfsprotocol = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

	Status = gBS->HandleProtocol(ImageHandle, &loadedImageProtocol, (void**)&loadedImage);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to locate image handle \n");
		return false;
	}

	Status =
		gBS->HandleProtocol(loadedImage->DeviceHandle, &sfsprotocol, (VOID**)&SimpleFileSystem);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to locate file system protocol \n");
		return false;
	}

	Status = SimpleFileSystem->OpenVolume(SimpleFileSystem, &Root);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to open the root directory \n");
		return false;
	}

	/* A miss here is the normal case (no marker file), so stay silent
	 * and let the caller fall back to its default path. --axiss */
	Status = Root->Open(Root, &File, Filename, EFI_FILE_MODE_READ, 0);
	Root->Close(Root);
	if (EFI_ERROR(Status))
		return false;

	File->Close(File);
	return true;
}

/*
 * XECloseFile -- Close an opened file
 * it just free up the buffer allocated
 * @param file -- Pointer to the file structure
 */
VOID XECloseFile(XEFile* file) {
	if (!file)
		return;
	if (file->kBuffer && file->PageCount)
		gBS->FreePages((EFI_PHYSICAL_ADDRESS)file->kBuffer, file->PageCount);
	XEFreePool(file);
}
