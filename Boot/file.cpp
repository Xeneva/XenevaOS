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

/*
 * XEOpenAndReadFile -- open and reads a file
 * @param ImageHandle -- Image handle passed by EFI firmware
 * @param Filename -- name and path of the file
 */
XEFile* XEOpenAndReadFile(EFI_HANDLE ImageHandle,CHAR16* Filename) {
	EFI_STATUS Status;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* SimpleFileSystem;
	EFI_GUID loadedImageProtocol = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_LOADED_IMAGE* loadedImage;
	EFI_FILE_PROTOCOL* Root;
	EFI_FILE_PROTOCOL* File;
	EFI_FILE_INFO* FileInfo;
	UINTN FileInfoSize = 0;
	UINT64 FileSize;

	EFI_PHYSICAL_ADDRESS Buffer = 0;
	XEFile* xefile;

	EFI_GUID sfsprotocol = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;

	Status = gBS->HandleProtocol(ImageHandle, &loadedImageProtocol, (void**)&loadedImage);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to locate image handle \n");
		return 0;
	}


	Status = gBS->HandleProtocol(loadedImage->DeviceHandle, &sfsprotocol, (VOID**)&SimpleFileSystem);
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
		return 0;
	}

	Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, NULL);
	if (Status == EFI_BUFFER_TOO_SMALL) {
		Status = gBS->AllocatePool(EfiLoaderData, FileInfoSize, (VOID**)&FileInfo);
		if (EFI_ERROR(Status)) {
			XEGuiPrint("Failed to allocate buffer for file metadata \n");
			File->Close(File);
			return 0;
		}
	}

	Status = File->GetInfo(File, &gEfiFileInfoGuid, &FileInfoSize, FileInfo);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to get file metadata \n");
		gBS->FreePool(FileInfo);
		File->Close(File);
		return 0;
	}

	FileSize = FileInfo->FileSize;
	gBS->FreePool(FileInfo);

	XEGuiPrint("Loading file of num bytes : %d , pages : %d \r\n", FileSize, EFI_SIZE_TO_PAGES(FileSize));
	Status = gBS->AllocatePages(AllocateAnyPages,EfiLoaderData,EFI_SIZE_TO_PAGES(FileSize), &Buffer);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to allocate buffer for file content \n");
		File->Close(File);
		return 0;
	}

	XEGuiPrint("Reading file Buffer : %x \r\n", &Buffer);
	Status = File->Read(File, &FileSize, (VOID*)Buffer);
	if (EFI_ERROR(Status)) {
		XEGuiPrint("Failed to read file \n");
		File->Close(File);
		return 0;
	}

	xefile = (XEFile*)XEAllocatePool(sizeof(XEFile));
	if (!xefile) {
		XEGuiPrint("Failed to allocate file data structure \n");
		File->Close(File);
		return 0;
	}
	memset(xefile, 0, sizeof(XEFile));
	xefile->kBuffer = (void*)Buffer;
	xefile->FileSize = FileSize;
	File->Close(File);
	Root->Close(Root);
	return xefile;
}

/*
 * XECloseFile -- Close an opened file
 * it just free up the buffer allocated
 * @param file -- Pointer to the file structure
 */
VOID XECloseFile(XEFile *file) {
	gBS->FreePool(file->kBuffer);
	XEFreePool(file);
}