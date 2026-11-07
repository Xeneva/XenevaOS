//
// usbdevice.c
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
#include "uspi/usbdevice.h"
#include "uspi/dwhcidevice.h"
#include "uspi/usbendpoint.h"
#include "uspi/usbdevicefactory.h"
#include "uspios.h"
#include "uspi/util.h"
#include "uspi/stdarg.h"
#include "uspi/assert.h"
#include <Drivers/uart.h>

#define MAX_CONFIG_DESC_SIZE		512		// best guess

typedef struct TConfigurationHeader
{
	TUSBConfigurationDescriptor Configuration;
	TUSBInterfaceDescriptor	    Interface;
}
TConfigurationHeader;

void USBDeviceSetAddress (TUSBDevice *pThis, u8 ucAddress);

static const char FromDevice[] = "usbdev";

static u8 s_ucNextAddress = USB_FIRST_DEDICATED_ADDRESS;

void USBDevice (TUSBDevice *pThis, struct TDWHCIDevice *pHost, TUSBSpeed Speed,
		boolean bSplitTransfer, u8 ucHubAddress, u8 ucHubPortNumber)
{
	assert (pThis != 0);

	pThis->m_pHost = pHost;
	pThis->m_ucAddress = USB_DEFAULT_ADDRESS;
	pThis->m_Speed = Speed;
	pThis->m_pEndpoint0 = 0;
	pThis->m_bSplitTransfer = bSplitTransfer;
	pThis->m_ucHubAddress = ucHubAddress;
	pThis->m_ucHubPortNumber = ucHubPortNumber;
	pThis->m_pDeviceDesc = 0;
	pThis->m_pConfigDesc = 0;
	pThis->m_pConfigParser = 0;

	assert (pThis->m_pHost != 0);

	assert (pThis->m_pEndpoint0 == 0);
	pThis->m_pEndpoint0 = (TUSBEndpoint *) malloc (sizeof (TUSBEndpoint));
	assert (pThis->m_pEndpoint0 != 0);
	USBEndpoint (pThis->m_pEndpoint0, pThis);
	
	assert (ucHubPortNumber >= 1);

	USBString (&pThis->m_ManufacturerString, pThis);
	USBString (&pThis->m_ProductString, pThis);

	for (unsigned nFunction = 0; nFunction < USBDEV_MAX_FUNCTIONS; nFunction++)
	{
		pThis->m_pFunction[nFunction] = 0;
	}
}

void _USBDevice (TUSBDevice *pThis)
{
	assert (pThis != 0);

	for (unsigned nFunction = 0; nFunction < USBDEV_MAX_FUNCTIONS; nFunction++)
	{
		if (pThis->m_pFunction[nFunction] != 0)
		{
			_USBFunction (pThis->m_pFunction[nFunction]);
			free (pThis->m_pFunction[nFunction]);
			pThis->m_pFunction[nFunction] = 0;
		}
	}

	if (pThis->m_pConfigParser != 0)
	{
		_USBConfigurationParser (pThis->m_pConfigParser);
		free (pThis->m_pConfigParser);
		pThis->m_pConfigParser = 0;
	}

	if (pThis->m_pConfigDesc != 0)
	{
		//free (pThis->m_pConfigDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pConfigDesc);
		pThis->m_pConfigDesc = 0;
	}

	if (pThis->m_pDeviceDesc != 0)
	{
		//free (pThis->m_pDeviceDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pDeviceDesc);
		pThis->m_pDeviceDesc = 0;
	}

	if (pThis->m_pEndpoint0 != 0)
	{
		_USBEndpoint (pThis->m_pEndpoint0);
		free (pThis->m_pEndpoint0);
		pThis->m_pEndpoint0 = 0;
	}

	pThis->m_pHost = 0;

	_USBString (&pThis->m_ProductString);
	_USBString (&pThis->m_ManufacturerString);
}

boolean USBDeviceInitialize (TUSBDevice *pThis)
{
	assert (pThis != 0);

	assert (pThis->m_pDeviceDesc == 0);
	pThis->m_pDeviceDesc = (TUSBDeviceDescriptor*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL); // malloc(sizeof(TUSBDeviceDescriptor));
	assert (pThis->m_pDeviceDesc != 0);

	assert (pThis->m_pHost != 0);
	assert (pThis->m_pEndpoint0 != 0);
	
	assert (sizeof *pThis->m_pDeviceDesc >= USB_DEFAULT_MAX_PACKET_SIZE);
	if (DWHCIDeviceGetDescriptor (pThis->m_pHost, pThis->m_pEndpoint0,
				    DESCRIPTOR_DEVICE, DESCRIPTOR_INDEX_DEFAULT,
				    pThis->m_pDeviceDesc, USB_DEFAULT_MAX_PACKET_SIZE, REQUEST_IN)
	    != USB_DEFAULT_MAX_PACKET_SIZE)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Cannot get device descriptor (short)");

		//free (pThis->m_pDeviceDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pDeviceDesc);
		pThis->m_pDeviceDesc = 0;

		return FALSE;
	}

	if (   pThis->m_pDeviceDesc->bLength	     != sizeof *pThis->m_pDeviceDesc
	    || pThis->m_pDeviceDesc->bDescriptorType != DESCRIPTOR_DEVICE)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Invalid device descriptor");

		free (pThis->m_pDeviceDesc);
		pThis->m_pDeviceDesc = 0;

		return FALSE;
	}

	USBEndpointSetMaxPacketSize (pThis->m_pEndpoint0, pThis->m_pDeviceDesc->bMaxPacketSize0);

	if (DWHCIDeviceGetDescriptor (pThis->m_pHost, pThis->m_pEndpoint0,
				    DESCRIPTOR_DEVICE, DESCRIPTOR_INDEX_DEFAULT,
				    pThis->m_pDeviceDesc, sizeof *pThis->m_pDeviceDesc, REQUEST_IN)
	    != (int) sizeof *pThis->m_pDeviceDesc)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Cannot get device descriptor");

		//free (pThis->m_pDeviceDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pDeviceDesc);
		pThis->m_pDeviceDesc = 0;

		return FALSE;
	}

#ifndef NDEBUG
	//DebugHexdump (pThis->m_pDeviceDesc, sizeof *pThis->m_pDeviceDesc, FromDevice);
#endif
	
	u8 ucAddress = s_ucNextAddress++;
	if (ucAddress > USB_MAX_ADDRESS)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Too many devices");

		return FALSE;
	}

	if (!DWHCIDeviceSetAddress (pThis->m_pHost, pThis->m_pEndpoint0, ucAddress))
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Cannot set address %u", (unsigned) ucAddress);

		return FALSE;
	}
	
	USBDeviceSetAddress (pThis, ucAddress);
	//UARTDebugOut("[dwc2otg]: set address successfull :%d\r\n", ucAddress);

	if (   pThis->m_pDeviceDesc->iManufacturer != 0
	    || pThis->m_pDeviceDesc->iProduct != 0)
	{
		u16 usLanguageID = USBStringGetLanguageID (&pThis->m_ManufacturerString);

		if (pThis->m_pDeviceDesc->iManufacturer != 0)
		{
			USBStringGetFromDescriptor (&pThis->m_ManufacturerString,
						    pThis->m_pDeviceDesc->iManufacturer, usLanguageID);
		}

		if (pThis->m_pDeviceDesc->iProduct != 0)
		{
			USBStringGetFromDescriptor (&pThis->m_ProductString,
						    pThis->m_pDeviceDesc->iProduct, usLanguageID);
		}
	}

	assert (pThis->m_pConfigDesc == 0);
	pThis->m_pConfigDesc = (TUSBConfigurationDescriptor*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL); // malloc(sizeof(TUSBConfigurationDescriptor));
	assert (pThis->m_pConfigDesc != 0);

	if (DWHCIDeviceGetDescriptor (pThis->m_pHost, pThis->m_pEndpoint0,
				    DESCRIPTOR_CONFIGURATION, DESCRIPTOR_INDEX_DEFAULT,
				    pThis->m_pConfigDesc, sizeof *pThis->m_pConfigDesc, REQUEST_IN)
	    != (int) sizeof *pThis->m_pConfigDesc)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Cannot get configuration descriptor (short)");

		//free (pThis->m_pConfigDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pConfigDesc);
		pThis->m_pConfigDesc = 0;

		return FALSE;
	}
	//UARTDebugOut("Configuration descriptor device got \r\n");

	if (   pThis->m_pConfigDesc->bLength         != sizeof *pThis->m_pConfigDesc
	    || pThis->m_pConfigDesc->bDescriptorType != DESCRIPTOR_CONFIGURATION
	    || pThis->m_pConfigDesc->wTotalLength    >  MAX_CONFIG_DESC_SIZE)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Invalid configuration descriptor");
		
		//free (pThis->m_pConfigDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pConfigDesc);
		pThis->m_pConfigDesc = 0;

		return FALSE;
	}

	unsigned nTotalLength = pThis->m_pConfigDesc->wTotalLength;

	//free(pThis->m_pConfigDesc);
	AuPmmngrFree (pThis->m_pConfigDesc);

	pThis->m_pConfigDesc = (TUSBConfigurationDescriptor*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);// malloc (nTotalLength);
	assert (pThis->m_pConfigDesc != 0);

	if (DWHCIDeviceGetDescriptor (pThis->m_pHost, pThis->m_pEndpoint0,
				    DESCRIPTOR_CONFIGURATION, DESCRIPTOR_INDEX_DEFAULT,
				    pThis->m_pConfigDesc, nTotalLength, REQUEST_IN)
	    != (int) nTotalLength)
	{
		USBDeviceLogWrite (pThis, LOG_ERROR, "Cannot get configuration descriptor");

		//free (pThis->m_pConfigDesc);
		AuPmmngrReleasePage((uint64_t)pThis->m_pConfigDesc);
		pThis->m_pConfigDesc = 0;

		return FALSE;
	}

#ifndef NDEBUG
	//DebugHexdump (pThis->m_pConfigDesc, nTotalLength, FromDevice);
#endif
	//UARTDebugOut("Parsing config descriptor total Len: %d\r\n", pThis->m_pConfigDesc->wTotalLength);
	
	assert (pThis->m_pConfigParser == 0);
	pThis->m_pConfigParser = malloc (sizeof (TUSBConfigurationParser));
	assert (pThis->m_pConfigParser != 0);
	USBConfigurationParser (pThis->m_pConfigParser, pThis->m_pConfigDesc, nTotalLength);
	
	if (!USBConfigurationParserIsValid (pThis->m_pConfigParser))
	{
		USBDeviceConfigurationError (pThis, FromDevice);

		return FALSE;
	}

	TString *pNames	= USBDeviceGetNames (pThis);
	assert (pNames != 0);
	//USBDeviceLogWrite (pThis, LOG_NOTICE, "Device %s found \r\n", StringGet (pNames));
	_String (pNames);
	 free (pNames);

	unsigned nFunction = 0;
	u8 ucInterfaceNumber = 0;

	TUSBInterfaceDescriptor *pInterfaceDesc;
	while ((pInterfaceDesc = (TUSBInterfaceDescriptor *) USBConfigurationParserGetDescriptor (pThis->m_pConfigParser, DESCRIPTOR_INTERFACE)) != 0)
	{
		if (pInterfaceDesc->bInterfaceNumber > ucInterfaceNumber)
		{
			ucInterfaceNumber = pInterfaceDesc->bInterfaceNumber;
		}

		if (pInterfaceDesc->bInterfaceNumber != ucInterfaceNumber)
		{
			USBDeviceLogWrite (pThis, LOG_DEBUG, "Alternate setting %u ignored",
					   (unsigned) pInterfaceDesc->bAlternateSetting);

			continue;
		}

		//UARTDebugOut("pInterfaceDesc class: %x \r\n", pInterfaceDesc->bInterfaceClass);
		//UARTDebugOut("pInterfaceDesc iNumber : %x \r\n", pInterfaceDesc->bInterfaceSubClass);
		assert (pThis->m_pConfigParser != 0);
		assert (pThis->m_pFunction[nFunction] == 0);
		pThis->m_pFunction[nFunction] = (TUSBFunction *) malloc (sizeof (TUSBFunction));
		assert (pThis->m_pFunction[nFunction] != 0);
		USBFunction (pThis->m_pFunction[nFunction], pThis, pThis->m_pConfigParser);

		TUSBFunction *pChild = 0;

		if (nFunction == 0)
		{
			pChild = USBDeviceFactoryGetDevice (pThis->m_pFunction[nFunction], USBDeviceGetName (pThis, DeviceNameVendor));
			if (pChild == 0)
			{
				pChild = USBDeviceFactoryGetDevice (pThis->m_pFunction[nFunction], USBDeviceGetName (pThis, DeviceNameDevice));
			}
		}

		if (pChild == 0)
		{
			TString *pName = USBFunctionGetInterfaceName (pThis->m_pFunction[nFunction]);
			assert (pName != 0);
			if (StringCompare (pName, "unknown") != 0)
			{
				USBDeviceLogWrite (pThis, LOG_NOTICE, "Interface %s found \r\n", StringGet (pName));

				pChild = USBDeviceFactoryGetDevice (pThis->m_pFunction[nFunction], pName);
			}
			else
			{
				_String (pName);
				free (pName);
			}
		}

		_USBFunction (pThis->m_pFunction[nFunction]);
		free (pThis->m_pFunction[nFunction]);
		pThis->m_pFunction[nFunction] = 0;

		if (pChild == 0)
		{
			USBDeviceLogWrite (pThis, LOG_WARNING, "Function is not supported \r\n");

			continue;
		}

		pThis->m_pFunction[nFunction] = pChild;

		if (++nFunction == USBDEV_MAX_FUNCTIONS)
		{
			USBDeviceLogWrite (pThis, LOG_WARNING, "Too many functions per device\r\n");

			break;
		}

		ucInterfaceNumber++;
	}

	if (nFunction == 0)
	{
		USBDeviceLogWrite (pThis, LOG_WARNING, "Device has no supported function \r\n");

		return FALSE;
	}

	return TRUE;
}

boolean USBDeviceConfigure (TUSBDevice *pThis)
{
	assert (pThis != 0);

	assert (pThis->m_pHost != 0);
	assert (pThis->m_pEndpoint0 != 0);

	if (pThis->m_pConfigDesc == 0)		// not initialized
	{
		return FALSE;
	}
	//UARTDebugOut("Configuring usb device %d\r\n", pThis->m_pConfigDesc->bConfigurationValue);
	if (!DWHCIDeviceSetConfiguration (pThis->m_pHost, pThis->m_pEndpoint0, pThis->m_pConfigDesc->bConfigurationValue))
	{
		//UARTDebugOut("Failed to configure device \r\n");
		USBDeviceLogWrite (pThis, LOG_ERROR, "Cannot set configuration (%u)",
			     (unsigned) pThis->m_pConfigDesc->bConfigurationValue);

		return FALSE;
	}

	boolean bResult = FALSE;
	//UARTDebugOut("Configured the device successfully %d\r\n", pThis->m_pConfigDesc->bConfigurationValue);
	for (unsigned nFunction = 0; nFunction < USBDEV_MAX_FUNCTIONS; nFunction++)
	{
		if (pThis->m_pFunction[nFunction] != 0)
		{
			if (!(*pThis->m_pFunction[nFunction]->Configure) (pThis->m_pFunction[nFunction]))
			{
				//LogWrite (LOG_ERROR, "Cannot configure device");

				_USBFunction (pThis->m_pFunction[nFunction]);
				free (pThis->m_pFunction[nFunction]);
				pThis->m_pFunction[nFunction] = 0;
			}
			else
			{
				bResult = TRUE;
			}
		}
	}

	return bResult;
}

TString *USBDeviceGetName (TUSBDevice *pThis, TDeviceNameSelector Selector)
{
	assert (pThis != 0);

	TString *pString = malloc (sizeof (TString));
	assert (pString != 0);
	String (pString);
	
	switch (Selector)
	{
	case DeviceNameVendor:
		assert (pThis->m_pDeviceDesc != 0);
		StringFormat (pString, "ven%x-%x",
				 (unsigned) pThis->m_pDeviceDesc->idVendor,
				 (unsigned) pThis->m_pDeviceDesc->idProduct);
		break;
		
	case DeviceNameDevice:
		assert (pThis->m_pDeviceDesc != 0);
		if (   pThis->m_pDeviceDesc->bDeviceClass == 0
		    || pThis->m_pDeviceDesc->bDeviceClass == 0xFF)
		{
			goto unknown;
		}
		StringFormat (pString, "dev%x-%x-%x",
				 (unsigned) pThis->m_pDeviceDesc->bDeviceClass,
				 (unsigned) pThis->m_pDeviceDesc->bDeviceSubClass,
				 (unsigned) pThis->m_pDeviceDesc->bDeviceProtocol);
		break;
		
	default:
		assert (0);
	unknown:
		StringSet (pString, "unknown");
		break;
	}
	
	return pString;
}

TString *USBDeviceGetNames (TUSBDevice *pThis)
{
	assert (pThis != 0);

	TString *pResult = malloc (sizeof (TString));
	assert (pResult != 0);
	String (pResult);

	for (unsigned nSelector = DeviceNameVendor; nSelector < DeviceNameUnknown; nSelector++)
	{
		TString *pName = USBDeviceGetName (pThis, (TDeviceNameSelector) nSelector);
		assert (pName != 0);

		if (StringCompare (pName, "unknown") != 0)
		{
			if (StringGetLength (pResult) > 0)
			{
				StringAppend (pResult, ", ");
			}

			StringAppend (pResult, StringGet (pName));
		}

		_String (pName);
		free (pName);
	}

	if (StringGetLength (pResult) == 0)
	{
		StringSet (pResult, "unknown");
	}

	return pResult;
}

u8 USBDeviceGetAddress (TUSBDevice *pThis)
{
	assert (pThis != 0);
	return pThis->m_ucAddress;
}

TUSBSpeed USBDeviceGetSpeed (TUSBDevice *pThis)
{
	assert (pThis != 0);
	return pThis->m_Speed;
}

boolean USBDeviceIsSplit (TUSBDevice *pThis)
{
	assert (pThis != 0);
	return pThis->m_bSplitTransfer;
}

u8 USBDeviceGetHubAddress (TUSBDevice *pThis)
{
	assert (pThis != 0);
	return pThis->m_ucHubAddress;
}

u8 USBDeviceGetHubPortNumber (TUSBDevice *pThis)
{
	assert (pThis != 0);
	return pThis->m_ucHubPortNumber;
}

struct TUSBEndpoint *USBDeviceGetEndpoint0 (TUSBDevice *pThis)
{
	assert (pThis != 0);
	assert (pThis->m_pEndpoint0 != 0);
	return pThis->m_pEndpoint0;
}

struct TDWHCIDevice *USBDeviceGetHost (TUSBDevice *pThis)
{
	assert (pThis != 0);
	assert (pThis->m_pHost != 0);
	return pThis->m_pHost;
}

const TUSBDeviceDescriptor *USBDeviceGetDeviceDescriptor (TUSBDevice *pThis)
{
	assert (pThis != 0);
	assert (pThis->m_pDeviceDesc != 0);
	return pThis->m_pDeviceDesc;
}

const TUSBConfigurationDescriptor *USBDeviceGetConfigurationDescriptor (TUSBDevice *pThis)
{
	assert (pThis != 0);
	assert (pThis->m_pConfigDesc != 0);
	return pThis->m_pConfigDesc;
}

const TUSBDescriptor *USBDeviceGetDescriptor (TUSBDevice *pThis, u8 ucType)
{
	assert (pThis != 0);
	assert (pThis->m_pConfigParser != 0);
	return USBConfigurationParserGetDescriptor (pThis->m_pConfigParser, ucType);
}

void USBDeviceConfigurationError (TUSBDevice *pThis, const char *pSource)
{
	assert (pThis != 0);
	assert (pThis->m_pConfigParser != 0);
	USBConfigurationParserError (pThis->m_pConfigParser, pSource);
}

void USBDeviceSetAddress (TUSBDevice *pThis, u8 ucAddress)
{
	assert (pThis != 0);

	assert (ucAddress <= USB_MAX_ADDRESS);
	pThis->m_ucAddress = ucAddress;

	//USBDeviceLogWrite (pThis, LOG_DEBUG, "Device address set to %u", (unsigned) pThis->m_ucAddress);
}

extern void _store_stack_param(uint8_t* buffer);

void USBDeviceLogWrite (TUSBDevice *pThis, unsigned Severity, const char *pMessage, ...)
{
	uint64_t buffer[192];
	_store_stack_param((uint8_t*)buffer);
	assert (pThis != 0);
	assert (pMessage != 0);

	TString Source;
	String (&Source);
	StringFormat (&Source, "%s%u-%u", FromDevice, (unsigned) pThis->m_ucHubAddress,
		      (unsigned) pThis->m_ucHubPortNumber);

	va_list var = (va_list)buffer;
	//va_start (var, pMessage);

	TString Message;
	String (&Message);
	StringFormatV (&Message, pMessage, var);

	LogWrite (StringGet (&Source), Severity, StringGet (&Message));

	va_end (var);

	_String (&Message);
	_String (&Source);
}
