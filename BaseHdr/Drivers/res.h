/**
* @file res.h
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

#ifndef __RESOURCE_H__
#define __RESOURCE_H__

#include <stdint.h>
#include <Drivers/core.h>
#include <aurora.h>


typedef struct _clk_ {
	BordoisilaDriverResource res;
	uint64_t rate_hz;
	int (*enable)(struct _clk_* clk,  uint64_t rate);
	int (*disable)(struct _clk_* clk);
	uint64_t(*get_rate)(struct _clk_* clk);
}BordoisilaClk;


typedef struct _regulator_ {
	BordoisilaDriverResource res;
	uint32_t microvoltz;
	int (*enable)(struct _regulator_* reg);
	int (*disable)(struct _regulator_* r);
	int (*set_volt)(struct _regulator_* r, uint32_t mv);
	int (*get_volt)(struct _regulator_* r);
}BordoisilaRegulator;


typedef struct _pmdomain_ {
	BordoisilaDriverResource res;
	int (*power_on)(struct _pmdomain_* pmd);
	int (*power_down)(struct _pmdomain_* pmd);
	int (*parent_bus_enable)(struct _pmdomain_* pmd);
}BordoisilaPower;


/**
 * @brief BordoisilaDriverResourceRegister -- register a resource driver
 * @param res_ -- pointer to resource driver struct
 */
AU_EXTERN AU_EXPORT int BordoisilaDriverResourceRegister(BordoisilaDriverResource* res_);

/**
 * @brief BordoisilaGetDriverResource -- get a driver resource from global resource
 * list
 * @param name -- resource name coresponding to DT/ACPI name
 * @param type -- type of the resource
 */
AU_EXTERN AU_EXPORT BordoisilaDriverResource* BordoisilaGetDriverResource(const char* name, uint8_t type);

#endif