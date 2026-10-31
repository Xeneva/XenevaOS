/**
* @file core.c
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
#include <Log/klog.h>
#include <_null.h>
#include <string.h>

/* must also contain locking/unlocking */
BordoisilaDriver* _bdrivers;

/**
 * @brief BordoisilaDriverGetStateString -- return the driver state
 * in string
 * @param driver -- pointer to driver struct
 */
static char* BordoisilaDriverGetStateString(BordoisilaDriver* driver) {
	switch (driver->driver_state) {
	case B_DRIVER_STATE_UNBOUND:
		return "UNBOUND";
	case B_DRIVER_STATE_PROBED:
		return "PROBED";
	case B_DRIVER_STATE_FAILED:
		return "FAILED";
	default:
		return "NULL";
	}
}

/**
 * @brief BordoisilaDriverRegister -- register a driver
 * to core registry
 */
int BordoisilaDriverRegister(BordoisilaDriver* driver) {
	if (!driver)
		return 1;
	/** check for pre-registered driver with same compat string */
	if (BordoisilaGetDriverByCompat(driver->compat)) {
		BPrintK(BORDOISILA_WARN,
				"driver : %s is already registered, current state : %s \r\n",
				driver->name,
				BordoisilaDriverGetStateString(driver));
		return BORDOISILA_DRIVER_ALREADY_REGISTERED;
	}
	driver->driver_state = B_DRIVER_STATE_UNBOUND;
	driver->next = _bdrivers;
	_bdrivers = driver;
	return 0;
}

/**
 * @brief BordoisilaGetDriverByCompat -- match a driver by its
 * compat string
 * @param compat -- compatibility string, matches to DT/ACPI bindings
 */
BordoisilaDriver* BordoisilaGetDriverByCompat(char* compat) {
	for (BordoisilaDriver* drv = _bdrivers; drv != NULL; drv = drv->next)
		if (strcmp(drv->compat, compat) == 0)
			return drv;

	return NULL;
}

/**
 * @brief BordoisilaGetDriverByName -- match a driver by its name
 * @param name - name of the driver
 */
BordoisilaDriver* BordoisilaGetDriverByName(char* name) {
	for (BordoisilaDriver* drv = _bdrivers; drv != NULL; drv = drv->next) {
		if (strcmp(drv->name, name) == 0)
			return drv;
	}

	return NULL;
}

/**
 * @brief BordoisilaDriverProbeAll -- start probing all driver registered
 */
void BordoisilaDriverProbeAll() {
	int ret = 0;
	for (BordoisilaDriver* drv = _bdrivers; drv != NULL; drv = drv->next) {
		if (drv->driver_state != B_DRIVER_STATE_UNBOUND)
			continue;

		if (drv->probe) {
			BPrintK(BORDOISILA_INFO,
					"probing bordoisila driver : %s, compat : %s \r\n",
					_bdrivers->name,
					_bdrivers->compat);
			ret = drv->probe(drv);
			BPrintK(BORDOISILA_WARN,
					"driver %s probe completed with return value : %d \r\n",
					_bdrivers->name,
					ret);
		}
	}
}

/**
* ===================================================
 * Interface 
 * ==================================================
 */

/**
 * @brief BordoisilaDriverScan -- scan a bus, only applicable
 * if the driver is a bus
 * @param driver -- pointer to driver
 */
int BordoisilaDriverScan(BordoisilaDriver* driver) {
	if (!driver)
		return 1;
	if (driver->type == BORDOISILA_DRIVER_NORMAL)
		return -1;

	int rc = 0;
	if (driver->scan)
		rc = driver->scan(driver);

	return rc;
}

int BordoisilaDriverProbe(BordoisilaDriver* driver) {
	if (!driver)
		return 1;
	if (driver->probe)
		return driver->probe(driver);
	return 0;
}

/**
 * @brief BordoisilaDriverRemove -- remove a driver doesn't mean
 *it could be removed from memory, calling remove cleans up
 * driver's allocated resources, and decreaments internal
 * reference count of driver resources it was using like
 * (Clk, Regulator, Pin etc), also if the driver is 
 * shared between different module, before calling remove
 * just decrement reference count
 */
int BordoisilaDriverRemove(BordoisilaDriver* driver) {
	if (!driver)
		return 1;

	if (driver->driver_state != B_DRIVER_STATE_PROBED)
		return -1;

	/* check if reference count is already more than zero */
	bool ref = (driver->refcount > 0) ? 1 : 0;

	if (ref) {
		driver->refcount--;
		return 0;
	}

	int ret = 0;
	if (driver->remove)
		ret = driver->remove(driver);

	if (ret == 0)
		driver->driver_state = B_DRIVER_STATE_UNBOUND;

	return 0;
}

int BordoisilaDriverSuspend(BordoisilaDriver* driver) {
	if (!driver)
		return 1;

	if (driver->driver_state != B_DRIVER_STATE_PROBED)
		return -1;

	if (driver->suspend)
		return driver->suspend(driver);
	return 0;
}

int BordoisilaDriverResume(BordoisilaDriver* driver) {
	if (!driver)
		return 1;
	if (driver->driver_state != B_DRIVER_STATE_PROBED)
		return -1;
	if (driver->resume)
		return driver->resume(driver);
	return 0;
}