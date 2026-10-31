/**
* @file res.c
*
* BSD 2-Clause License
*
* Copyright (c) 2022-2026, Manas Kamal Choudhury
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

#include <Drivers/core.h>
#include <hashmap.h>
#include <_null.h>
#include <string.h>
#include <Log/klog.h>
#include <Drivers/res.h>

BordoisilaDriverResource* _gres;

/**
 * @brief BordoisilaDriverResourceRegister -- register a resource driver
 * @param res_ -- pointer to resource driver struct
 */
int BordoisilaDriverResourceRegister(BordoisilaDriverResource* res_) {
	for (BordoisilaDriverResource* res = _gres; res != NULL; res = res->next)
		if (strcmp(res->name, res_->name) == 0)
			return 1;

	res_->next = _gres;
	_gres = res_;
	return 0;
}

/**
 * @brief BordoisilaGetDriverResource -- get a driver resource from global resource
 * list
 * @param name -- resource name coresponding to DT/ACPI name
 * @param type -- type of the resource
 */
BordoisilaDriverResource* BordoisilaGetDriverResource(const char* name, uint8_t type) {
	for (BordoisilaDriverResource* res = _gres; res != NULL; res = res->next)
		if ((strcmp(res->name, name) == 0) && res->res_type == type)
			return res;

	return NULL;
}