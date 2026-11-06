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

#ifndef __XE_LDR_OBJECT_H__
#define __XE_LDR_OBJECT_H__

#include <stdint.h>

//#pragma pack(push,1)
typedef struct _XELDR_OBJ_ {
    char *objname;
	bool loaded;
	bool linked;
	uint32_t len;
	size_t load_addr;
	size_t entry_addr;
	struct _XELDR_OBJ_ *next;
	struct _XELDR_OBJ_* prev;
}XELoaderObject;
//#pragma pack(pop)


/*
* XELdrInitObjectList -- initialise the object
* list
*/
extern void XELdrInitObjectList();

/*
* XELdrCreateObj -- creates an loader object
* @param objname -- name of the object
*/
extern XELoaderObject* XELdrCreateObj(char* objname);

/*
* XELdrDestroyObject -- destroys an object
* @param obj -- object to destroy
*/
extern void XELdrDestroyObject(XELoaderObject *obj);

/*
* XELdrCheckObject -- Checks an object if its already
* in the list
* @param name -- name to check
*/
extern bool XELdrCheckObject(const char* name);

/*
* XELdrLoadObject -- loads an object
* @param obj
*/
extern int XELdrLoadObject(XELoaderObject *obj);

/*
* XELdrLoadAllObject -- loads all object
* from the list
*/
extern void XELdrLoadAllObject();

/*
* XELdrLinkAllObject -- link all object
* from the list to main object
*/
extern void XELdrLinkAllObject(XELoaderObject *mainobj);

/*
* XELdrGetObject-- gets an object by its name
* @param name -- name of the object
*/
extern XELoaderObject* XELdrGetObject(const char* name);

/*
* XELdrLinkDepObject -- verify and links all dependent objects except
* the main object
* @param mainobj -- main object
*/
extern void XELdrLinkDepObject(XELoaderObject *mainobj);

extern void XELdrPrintAllObject();

/*
* clear the object list
*/
extern void XELdrClearObjectList();
#endif