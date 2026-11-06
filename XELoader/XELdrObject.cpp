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

#include "XELdrObject.h"
#include <stdlib.h>
#include "pe_.h"
#include <string.h>

static XELoaderObject *obj_first;
static XELoaderObject *obj_last;


void XELoaderAddObject(XELoaderObject* obj) {
	obj->next = NULL;
	obj->prev = NULL;

	if (obj_first == NULL) {
		obj_last = obj;
		obj_first = obj;
	}
	else {
		obj_last->next = obj;
		obj->prev = obj_last;
	}
	obj_last = obj;
}

/*
* XELdrRemoveObject -- removes an object from the list
* @param obj -- Object to remove
*/
void XELdrRemoveObject(XELoaderObject* obj) {
	if (obj_first == NULL)
		return;

	if (obj == obj_first) {
		obj_first = obj_first->next;
	}
	else {
		obj->prev->next = obj->next;
	}

	if (obj == obj_last) {
		obj_last = obj->prev;
	}
	else {
		obj->next->prev = obj->prev;
	}
}


/*
 * XELdrCreateObj -- creates an loader object
 * @param objname -- name of the object
 */
XELoaderObject* XELdrCreateObj(char* objname) {
	XELoaderObject* obj = (XELoaderObject*)malloc(sizeof(XELoaderObject));
	memset(obj, 0, sizeof(XELoaderObject));
	obj->objname = (char*)malloc(strlen(objname) + 1);
	strcpy(obj->objname, objname);
	obj->loaded = false;
	obj->load_addr = 0;
	obj->entry_addr = 0;
	XELoaderAddObject(obj);
	return obj;
}

/*
 * XELdrDestroyObject -- destroys an object
 * @param obj -- object to destroy
 */
void XELdrDestroyObject(XELoaderObject *obj) {
	XELdrRemoveObject(obj);
	free(obj->objname);
	free(obj);
}

/*
 * XELdrInitObjectList -- initialise the object
 * list
 */
void XELdrInitObjectList() {
	obj_first = NULL;
	obj_last = NULL;
}

/*
 * XELdrCheckObject -- Checks an object if its already
 * in the list
 * @param name -- name to check
 */
bool XELdrCheckObject(const char* name) {
	XELoaderObject* obj_ = NULL;
	for (obj_ = obj_first; obj_ != NULL; obj_ = obj_->next) {
		char* p = strchr(obj_->objname, '/');
		if (p)
			p++;
		else
			p = obj_->objname;
		if (strcmp(p, name) == 0)
			return true;
	}
	return false;
}

/*
 * XELdrGetObject-- gets an object by its name
 * @param name -- name of the object
 */
XELoaderObject* XELdrGetObject(const char* name) {
	XELoaderObject* obj_ = NULL;
	for (obj_ = obj_first; obj_ != NULL; obj_ = obj_->next) {
		char* p = strchr(obj_->objname, '/');
		if (p)
			p++;
		if (strcmp(p, name) == 0)
			return obj_;
	}
	return NULL;
}

/*
* XELdrLoadAllObject -- loads all object
* from the list
*/
void XELdrLoadAllObject() {
	XELoaderObject* obj_ = NULL;
	for (obj_ = obj_first; obj_ != NULL; obj_ = obj_->next) {
		if (obj_->loaded == false)
			XELdrLoadObject(obj_);
	}
}

/*
* XELdrLinkAllObject -- link all object
* from the list to main object
*/
void XELdrLinkAllObject(XELoaderObject *mainobj) {
	XELdrLinkPE((void*)mainobj->load_addr);
}

/*
 * XELdrLinkDepObject -- verify and links all dependent objects except
 * the main object
 * @param mainobj -- main object
 */
void XELdrLinkDepObject(XELoaderObject *mainobj) {
	XELoaderObject* obj_ = NULL;
	for (obj_ = obj_first; obj_ != NULL; obj_ = obj_->next) {
		if (obj_ == mainobj)
			continue;
		XELdrLinkDependencyPE(obj_);
	}
}

void XELdrPrintAllObject() {
	for (XELoaderObject *obj_ = obj_first; obj_ != NULL; obj_ = obj_->next) 
		_KePrint("Object -> %s load_addr -> %x \r\n", obj_->objname, obj_->load_addr);
}

/*
 * clear the object list
 */
void XELdrClearObjectList() {
	XELoaderObject* obj_ = obj_first;
	while (obj_) {
		XELoaderObject *temp = obj_->next;
		free(obj_->objname);
		free(obj_);
		obj_ = temp;
	}
}