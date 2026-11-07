//
// usbstandardhub.c
//
// USPi - An USB driver for Raspberry Pi written in C
// Copyright (C) 2014-2018  R. Stange <rsta2@o2online.de>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include "uspi/usbstandardhub.h"
#include "uspi/usbdevicefactory.h"
#include "uspios.h"
#include "uspi/macros.h"
#include "uspi/assert.h"
#include <Drivers/uart.h>

boolean USBStandardHubEnumeratePorts (TUSBStandardHub *pThis);

static const char FromHub[] = "usbhub";
extern bool _dwhcidebug;

void USBStandardHub (TUSBStandardHub *pThis, TUSBFunction *pDevice)
{
	assert (pThis != 0);

	USBFunctionCopy (&pThis->m_USBFunction, pDevice);
	pThis->m_USBFunction.Configure = USBStandardHubConfigure;
	
	pThis->m_pHubDesc = 0;
	pThis->m_nPorts = 0;

	for (unsigned nPort = 0; nPort < USB_HUB_MAX_PORTS; nPort++)
	{
		pThis->m_pDevice[nPort] = 0;
		pThis->m_pStatus[nPort] = 0;
	}
	_dwhcidebug = true;
}

void _USBStandardHub (TUSBStandardHub *pThis)
{
	assert (pThis != 0);

	for (unsigned nPort = 0; nPort < pThis->m_nPorts; nPort++)
	{
		if (pThis->m_pStatus[nPort] != 0)
		{
			free (pThis->m_pStatus[nPort]);
			pThis->m_pStatus[nPort] = 0;
		}

		if (pThis->m_pDevice[nPort] != 0)
		{
			_USBDevice (pThis->m_pDevice[nPort]);
			free (pThis->m_pDevice[nPort]);
			pThis->m_pDevice[nPort] = 0;
		}
	}

	pThis->m_nPorts = 0;

	if (pThis->m_pHubDesc != 0)
	{
		free (pThis->m_pHubDesc);
		pThis->m_pHubDesc = 0;
	}

	_USBFunction (&pThis->m_USBFunction);
}

boolean USBStandardHubConfigure (TUSBFunction *pUSBFunction)
{
	TUSBStandardHub *pThis = (TUSBStandardHub *) pUSBFunction;
	assert (pThis != 0);

	if (USBFunctionGetNumEndpoints (&pThis->m_USBFunction) != 1)
	{
		USBFunctionConfigurationError (&pThis->m_USBFunction, FromHub);

		return FALSE;
	}

	const TUSBEndpointDescriptor *pEndpointDesc =
		(TUSBEndpointDescriptor *) USBFunctionGetDescriptor (&pThis->m_USBFunction, DESCRIPTOR_ENDPOINT);
	if (   pEndpointDesc == 0
	    || (pEndpointDesc->bEndpointAddress & 0x80) != 0x80		// input EP
	    || (pEndpointDesc->bmAttributes     & 0x3F) != 0x03)	// interrupt EP
	{
		USBFunctionConfigurationError (&pThis->m_USBFunction, FromHub);

		return FALSE;
	}

	if (!USBFunctionConfigure (&pThis->m_USBFunction))
	{
		LogWrite (FromHub, LOG_ERROR, "Cannot set interface");

		return FALSE;
	}

	TUSBHostController *pHost = USBFunctionGetHost (&pThis->m_USBFunction);
	assert (pHost != 0);

	assert (pThis->m_pHubDesc == 0);
	pThis->m_pHubDesc = (TUSBHubDescriptor*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL); // malloc(sizeof(TUSBHubDescriptor));
	assert (pThis->m_pHubDesc != 0);

	if (DWHCIDeviceGetDescriptor (pHost, USBFunctionGetEndpoint0 (&pThis->m_USBFunction),
					DESCRIPTOR_HUB, DESCRIPTOR_INDEX_DEFAULT,
					pThis->m_pHubDesc, sizeof *pThis->m_pHubDesc,
					REQUEST_IN | REQUEST_CLASS)
	   != (int) sizeof *pThis->m_pHubDesc)
	{
		LogWrite (FromHub, LOG_ERROR, "Cannot get hub descriptor");
		
		free (pThis->m_pHubDesc);
		pThis->m_pHubDesc = 0;
		
		return FALSE;
	}

#ifndef NDEBUG
	//DebugHexdump (pThis->m_pHubDesc, sizeof *pThis->m_pHubDesc, FromHub);
#endif

	pThis->m_nPorts = pThis->m_pHubDesc->bNbrPorts;
	if (pThis->m_nPorts > USB_HUB_MAX_PORTS)
	{
		LogWrite (FromHub, LOG_ERROR, "Too many ports (%u)", pThis->m_nPorts);
		
		free (pThis->m_pHubDesc);
		pThis->m_pHubDesc = 0;
		
		return FALSE;
	}

	if (!USBStandardHubEnumeratePorts (pThis))
	{
		LogWrite (FromHub, LOG_ERROR, "Port enumeration failed");

		return FALSE;
	}

	return TRUE;
}

boolean USBStandardHubEnumeratePorts (TUSBStandardHub *pThis)
{
	assert (pThis != 0);

	TUSBHostController *pHost = USBFunctionGetHost (&pThis->m_USBFunction);
	assert (pHost != 0);
	
	TUSBEndpoint *pEndpoint0 = USBFunctionGetEndpoint0 (&pThis->m_USBFunction);
	assert (pEndpoint0 != 0);

	assert (pThis->m_nPorts > 0);

	// first power on all ports
	for (unsigned nPort = 0; nPort < pThis->m_nPorts; nPort++)
	{
		if (DWHCIDeviceControlMessage (pHost, pEndpoint0,
			REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
			SET_FEATURE, PORT_POWER, nPort+1, 0, 0) < 0)
		{
			LogWrite (FromHub, LOG_ERROR, "Cannot power port %u", nPort+1);

			return FALSE;
		}
	}

	// pThis->m_pHubDesc->bPwrOn2PwrGood delay seems to be not enough
	// for some low speed devices, so we use the maximum here
	MsDelay (510);

	
	// now detect devices, reset and initialize them
	for (unsigned nPort = 0; nPort < pThis->m_nPorts; nPort++)
	{
		assert (pThis->m_pStatus[nPort] == 0);
		pThis->m_pStatus[nPort] = (void*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);// malloc (sizeof (TUSBPortStatus));
		assert (pThis->m_pStatus[nPort] != 0);

		if (DWHCIDeviceControlMessage (pHost, pEndpoint0,
			REQUEST_IN | REQUEST_CLASS | REQUEST_TO_OTHER,
			GET_STATUS, 0, nPort+1, pThis->m_pStatus[nPort], 4) != 4)
		{
			LogWrite (FromHub, LOG_ERROR, "Cannot get status of port %u", nPort+1);

			continue;
		}

		assert (pThis->m_pStatus[nPort]->wPortStatus & PORT_POWER__MASK);
		if (!(pThis->m_pStatus[nPort]->wPortStatus & PORT_CONNECTION__MASK))
		{
			continue;
		}

		if (DWHCIDeviceControlMessage (pHost, pEndpoint0,
			REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
			SET_FEATURE, PORT_RESET, nPort+1, 0, 0) < 0)
		{
			LogWrite (FromHub, LOG_ERROR, "Cannot reset port %d", nPort+1);

			continue;
		}

		MsDelay (100);
		
		if (DWHCIDeviceControlMessage (pHost, pEndpoint0,
			REQUEST_IN | REQUEST_CLASS | REQUEST_TO_OTHER,
			GET_STATUS, 0, nPort+1, pThis->m_pStatus[nPort], 4) != 4)
		{
			return FALSE;
		}

		//LogWrite (FromHub, LOG_DEBUG, "Port %u status is 0x%04X", nPort+1, (unsigned) pThis->m_pStatus[nPort]->wPortStatus);
		
		if (!(pThis->m_pStatus[nPort]->wPortStatus & PORT_ENABLE__MASK))
		{
			LogWrite (FromHub, LOG_ERROR, "Port %d is not enabled", nPort+1);

			continue;
		}

		// check for over-current
		if (pThis->m_pStatus[nPort]->wPortStatus & PORT_OVER_CURRENT__MASK)
		{
			DWHCIDeviceControlMessage (pHost, pEndpoint0,
				REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
				CLEAR_FEATURE, PORT_POWER, nPort+1, 0, 0);

			LogWrite (FromHub, LOG_ERROR, "Over-current condition on port %d", nPort+1);

			return FALSE;
		}

		TUSBSpeed Speed = USBSpeedUnknown;
		if (pThis->m_pStatus[nPort]->wPortStatus & PORT_LOW_SPEED__MASK)
		{
			Speed = USBSpeedLow;
		}
		else if (pThis->m_pStatus[nPort]->wPortStatus & PORT_HIGH_SPEED__MASK)
		{
			Speed = USBSpeedHigh;
		}
		else
		{
			Speed = USBSpeedFull;
		}

		TUSBDevice *pHubDevice = USBFunctionGetDevice (&pThis->m_USBFunction);
		assert (pHubDevice != 0);

		boolean bSplit     = USBDeviceIsSplit (pHubDevice);
		u8 ucHubAddress    = USBDeviceGetHubAddress (pHubDevice);
		u8 ucHubPortNumber = USBDeviceGetHubPortNumber (pHubDevice);

		// Is this the first high-speed hub with a non-high-speed device following in chain?
		if (   !bSplit
		    && USBDeviceGetSpeed (pHubDevice) == USBSpeedHigh
		    && Speed < USBSpeedHigh)
		{
			// Then enable split transfers with this hub port as translator.
			bSplit          = TRUE;
			ucHubAddress    = USBDeviceGetAddress (pHubDevice);
			ucHubPortNumber = nPort+1;
		}

		bSplit = TRUE;
		ucHubAddress = USBDeviceGetAddress(pHubDevice);
		ucHubPortNumber = nPort + 1;


		// first create default device
		assert (pThis->m_pDevice[nPort] == 0);
		pThis->m_pDevice[nPort] = malloc (sizeof (TUSBDevice));
		assert (pThis->m_pDevice[nPort] != 0);
		USBDevice (pThis->m_pDevice[nPort], pHost, Speed, bSplit, ucHubAddress, ucHubPortNumber);

		if (!USBDeviceInitialize (pThis->m_pDevice[nPort]))
		{
			UARTDebugOut("Failed to initialize USB Device \r\n");
			_USBDevice (pThis->m_pDevice[nPort]);
			free (pThis->m_pDevice[nPort]);
			pThis->m_pDevice[nPort] = 0;

			continue;
		}
	}

	// now configure devices
	for (unsigned nPort = 0; nPort < pThis->m_nPorts; nPort++)
	{
		if (pThis->m_pDevice[nPort] == 0)
		{
			continue;
		}

		if (!USBDeviceConfigure (pThis->m_pDevice[nPort]))
		{
			LogWrite (FromHub, LOG_ERROR, "Port %d: Cannot configure device", nPort+1);

			_USBDevice (pThis->m_pDevice[nPort]);
			free (pThis->m_pDevice[nPort]);
			pThis->m_pDevice[nPort] = 0;

			continue;
		}

		LogWrite (FromHub, LOG_DEBUG, "Port %d: Device configured", nPort+1);
	}

	// again check for over-current
	TUSBHubStatus *pHubStatus = malloc (sizeof (TUSBHubStatus));
	assert (pHubStatus != 0);

	if (DWHCIDeviceControlMessage (pHost, pEndpoint0,
		REQUEST_IN | REQUEST_CLASS,
		GET_STATUS, 0, 0, pHubStatus, sizeof *pHubStatus) != (int) sizeof *pHubStatus)
	{
		LogWrite (FromHub, LOG_ERROR, "Cannot get hub status");

		free (pHubStatus);

		return FALSE;
	}

	if (pHubStatus->wHubStatus & HUB_OVER_CURRENT__MASK)
	{
		for (unsigned nPort = 0; nPort < pThis->m_nPorts; nPort++)
		{
			DWHCIDeviceControlMessage (pHost, pEndpoint0,
				REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
				CLEAR_FEATURE, PORT_POWER, nPort+1, 0, 0);
		}

		LogWrite (FromHub, LOG_ERROR, "Hub over-current condition");

		free (pHubStatus);

		return FALSE;
	}

	free (pHubStatus);
	pHubStatus = 0;

	boolean bResult = TRUE;

	for (unsigned nPort = 0; nPort < pThis->m_nPorts; nPort++)
	{
		if (DWHCIDeviceControlMessage (pHost, pEndpoint0,
			REQUEST_IN | REQUEST_CLASS | REQUEST_TO_OTHER,
			GET_STATUS, 0, nPort+1, pThis->m_pStatus[nPort], 4) != 4)
		{
			continue;
		}

		if (pThis->m_pStatus[nPort]->wPortStatus & PORT_OVER_CURRENT__MASK)
		{
			DWHCIDeviceControlMessage (pHost, pEndpoint0,
				REQUEST_OUT | REQUEST_CLASS | REQUEST_TO_OTHER,
				CLEAR_FEATURE, PORT_POWER, nPort+1, 0, 0);

			LogWrite (FromHub, LOG_ERROR, "Over-current condition on port %d", nPort+1);

			bResult = FALSE;
		}
	}

	return bResult;
}
