/**
* @file core.h
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

#ifndef __CORE_H__
#define __CORE_H__

#include <stdint.h>
#include <aurora.h>

/* core system with all device driver registration
 * and data structures 
 */

#define BORDOISILA_DRIVER_RES_CLK       0
#define BORDOISILA_DRIVER_RES_GATE      1
#define BORDOISILA_DRIVER_RES_POWER     2
#define BORDOISILA_DRIVER_RES_REGULATOR 3
#define BORDOISILA_DRIVER_RES_PIN       4
#define BORDOISILA_DRIVER_RES_BUS       5

#define BORDOISILA_DRIVER_ALREADY_REGISTERED -2

typedef struct _bordoisila_drvcore_res_ {
	const char name[42];
	uint8_t res_type;
	void* data;
	int ref_count;
	bool is_running;
	struct _bordoisila_drvcore_res_* next;
}BordoisilaDriverResource;

#define B_DRIVER_STATE_PROBED  0
#define B_DRIVER_STATE_FAILED  1
#define B_DRIVER_STATE_UNBOUND 2

enum driver_type {
	BORDOISILA_DRIVER_NORMAL,
	BORDOISILA_DRIVER_BUS_I2C,
	BORDOISILA_DRIVER_BUS_SPI,
	BORDOISILA_DRIVER_BUS_USB,
};


typedef struct _bordoisila_driver_ {
	const char* name;
	const char* compat; //match list in DT/acpi bindings
	BordoisilaDriverResource* resources;
	int num_resource;
	enum driver_type type;

	/* Ops*/
	int (*scan)(struct _bordoisila_driver_* dev);
	int (*probe)(struct _bordoisila_driver_* dev);
	int (*remove)(struct _bordoisila_driver_* dev);
	int (*suspend)(struct _bordoisila_driver_* dev);
	int (*resume)(struct _bordoisila_driver_* dev);

	void* priv;
	uint8_t driver_state;
	int refcount;
	struct _bordoisila_driver_* parent;
	struct _bordoisila_driver_* next;
}BordoisilaDriver;


/**
 * @brief BordoisilaDriverRegister -- register a driver
 * to core registry
 */
AU_EXTERN AU_EXPORT int BordoisilaDriverRegister(BordoisilaDriver* driver);

/**
 * @brief BordoisilaGetDriverByCompat -- match a driver by its
 * compat string
 * @param compat -- compatibility string, matches to DT/ACPI bindings
 */
AU_EXTERN AU_EXPORT BordoisilaDriver* BordoisilaGetDriverByCompat(char* compat);

/**
 * @brief BordoisilaGetDriverByName -- match a driver by its name
 * @param name - name of the driver
 */
AU_EXTERN AU_EXPORT BordoisilaDriver* BordoisilaGetDriverByName(char* name);

/**
 * @brief BordoisilaDriverProbeAll -- start probing all driver registered
 */
extern void BordoisilaDriverProbeAll();

/**
 * @brief BordoisilaDriverScan -- scan a bus, only applicable
 * if the driver is a bus
 * @param driver -- pointer to driver
 */
AU_EXTERN AU_EXPORT int BordoisilaDriverScan(BordoisilaDriver* driver);

AU_EXTERN AU_EXPORT int BordoisilaDriverProbe(BordoisilaDriver* driver);

AU_EXTERN AU_EXPORT int BordoisilaDriverRemove(BordoisilaDriver* driver);

AU_EXTERN AU_EXPORT int BordoisilaDriverSuspend(BordoisilaDriver* driver);

AU_EXTERN AU_EXPORT int BordoisilaDriverResume(BordoisilaDriver* driver);


#endif
