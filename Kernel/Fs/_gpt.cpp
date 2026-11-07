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

#include <Fs/_gpt.h>
#include <Fs/Fat/Fat.h>
#include <Fs/_FsGUIDs.h>
#include <aucon.h>
#include <Fs/Ext2/ext2.h>

/* Need a function to verify the GUIDs with specific file system
 * if matched, then call the file system to detect other informations
 */

/* AuGUIDVerify -- compares two guid and return the result
 * @param g1 -- GUID one
 * @param g2 -- GUID two
 */
bool AuGUIDVerify(GUID g1, GUID g2) {
	bool first_part_good = (g1.Data1 == g2.Data1 && g1.Data2 == g2.Data2 &&
		g1.Data3 == g2.Data3);
	if (!first_part_good) return false;

	for (int i = 0; i < 8; ++i){
		if (g1.Data4[i] != g2.Data4[i]) 
			return false;
	}

	return true;
}


/*
 * AuGPTInitialise_FileSystem -- initialises specific file system
 * by looking partition GUID
 * @param vdisk -- Virtual disk partition
 */
void AuGPTInitialise_FileSystem(AuVDisk *vdisk) {
	GUID guid = WINDOWS_PARTITION_DATA_GUID;
	
	if (AuGUIDVerify(vdisk->part_guid, guid)) {
		AuTextOut("GPT Partition with Windows Partition data \n");

		/* Now we need to call all Microsoft file systems
		 * detect routine to verify what type of ms file system
		 * it includes, for now we only support FAT file system */
		FatInitialise(vdisk, "/");
	}

	guid = LINUX_HOME_PARTITION_GUID;

	if (AuGUIDVerify(vdisk->part_guid, guid)) {
		AuTextOut("GPT Partition with Linux file systems \n");
		/*
		 * Now call ext fs detect routine to verify ext version
		 * and initialise it
		 */
		Ext2Initialise(vdisk, "home");
	}

	guid = LINUX_FILESYSTEM;
	
	if (AuGUIDVerify(vdisk->part_guid, guid)) {
		AuTextOut("GPT Partition with Linux file systems \n");
		/* call ext2mount */
		Ext2Initialise(vdisk,"home");
	}

}

