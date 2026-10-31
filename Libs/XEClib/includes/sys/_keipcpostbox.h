/**
* @file _keipcpostbox.h
* 
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

#include <stdint.h>

#ifndef __KE_IPC_POSTBOX_H__
#define __KE_IPC_POSTBOX_H__

#define POSTBOX_CREATE		   401
#define POSTBOX_DESTROY		   402
#define POSTBOX_PUT_EVENT	   403
#define POSTBOX_GET_EVENT	   404
#define POSTBOX_CREATE_ROOT	   405
#define POSTBOX_GET_EVENT_ROOT 406

#define POSTBOX_NO_EVENT -1
#define POSTBOX_ROOT_ID	 1

//#pragma pack(push,1)
/*
* PostEvent -- event message structure
*/
typedef struct _post_event_ {
	uint8_t type;
	uint16_t to_id;
	uint16_t from_id;
	uint32_t dword;
	uint32_t dword2;
	uint32_t dword3;
	uint32_t dword4;
	uint32_t dword5;
	uint32_t dword6;
	uint32_t dword7;
	uint32_t dword8;
	uint32_t* pValue;
	uint32_t* pValue1;
	char* charValue;
	unsigned char* charValue2;
	char charValue3[100];
} PostEvent;
//#pragma pack(pop)

#endif