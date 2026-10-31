//
// lan7800.c
//
// This driver has been written based on the Linux lan78xx driver which is:
//	Copyright (C) 2015 Microchip Technology
//	Driver author: WOOJUNG HUH <woojung.huh@microchip.com>
//	Licensed under GPLv2
//
// USPi - An USB driver for Raspberry Pi written in C
// Copyright (C) 2018  R. Stange <rsta2@o2online.de>
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
#include "uspi/lan7800.h"
#include "uspi/usbhostcontroller.h"
#include "uspi/devicenameservice.h"
#include "uspi/util.h"
#include "uspi/assert.h"
#include "uspios.h"
#include <Drivers/uart.h>

// Sizes
#define HS_USB_PKT_SIZE			512

#define MAX_RX_FIFO_SIZE		(12 * 1024)
#define MAX_TX_FIFO_SIZE		(12 * 1024)
#define DEFAULT_BURST_CAP_SIZE		MAX_TX_FIFO_SIZE
#define DEFAULT_BULK_IN_DELAY		0x800

#define RX_HEADER_SIZE			(4 + 4 + 2)
#define TX_HEADER_SIZE			(4 + 4)

#define MAX_RX_FRAME_SIZE		(2*6 + 2 + 1500 + 4)

// USB vendor requests
#define WRITE_REGISTER			0xA0
#define READ_REGISTER			0xA1

// Device registers
#define ID_REV				0x000
	#define ID_REV_CHIP_ID_MASK		0xFFFF0000
		#define ID_REV_CHIP_ID_7800		0x7800
#define INT_STS				0x00C
	#define INT_STS_CLEAR_ALL		0xFFFFFFFF
#define HW_CFG				0x010
	#define HW_CFG_CLK125_EN		0x02000000
	#define HW_CFG_REFCLK25_EN		0x01000000
	#define HW_CFG_LED3_EN			0x00800000
	#define HW_CFG_LED2_EN			0x00400000
	#define HW_CFG_LED1_EN			0x00200000
	#define HW_CFG_LED0_EN			0x00100000
	#define HW_CFG_EEE_PHY_LUSU		0x00020000
	#define HW_CFG_EEE_TSU			0x00010000
	#define HW_CFG_NETDET_STS		0x00008000
	#define HW_CFG_NETDET_EN		0x00004000
	#define HW_CFG_EEM			0x00002000
	#define HW_CFG_RST_PROTECT		0x00001000
	#define HW_CFG_CONNECT_BUF		0x00000400
	#define HW_CFG_CONNECT_EN		0x00000200
	#define HW_CFG_CONNECT_POL		0x00000100
	#define HW_CFG_SUSPEND_N_SEL_MASK	0x000000C0
		#define HW_CFG_SUSPEND_N_SEL_2		0x00000000
		#define HW_CFG_SUSPEND_N_SEL_12N	0x00000040
		#define HW_CFG_SUSPEND_N_SEL_012N	0x00000080
		#define HW_CFG_SUSPEND_N_SEL_0123N	0x000000C0
	#define HW_CFG_SUSPEND_N_POL		0x00000020
	#define HW_CFG_MEF			0x00000010
	#define HW_CFG_ETC			0x00000008
	#define HW_CFG_LRST			0x00000002
	#define HW_CFG_SRST			0x00000001
#define PMT_CTL				0x014
	#define PMT_CTL_EEE_WAKEUP_EN		0x00002000
	#define PMT_CTL_EEE_WUPS		0x00001000
	#define PMT_CTL_MAC_SRST		0x00000800
	#define PMT_CTL_PHY_PWRUP		0x00000400
	#define PMT_CTL_RES_CLR_WKP_MASK	0x00000300
	#define PMT_CTL_RES_CLR_WKP_STS		0x00000200
	#define PMT_CTL_RES_CLR_WKP_EN		0x00000100
	#define PMT_CTL_READY			0x00000080
	#define PMT_CTL_SUS_MODE_MASK		0x00000060
	#define PMT_CTL_SUS_MODE_0		0x00000000
	#define PMT_CTL_SUS_MODE_1		0x00000020
	#define PMT_CTL_SUS_MODE_2		0x00000040
	#define PMT_CTL_SUS_MODE_3		0x00000060
	#define PMT_CTL_PHY_RST			0x00000010
	#define PMT_CTL_WOL_EN			0x00000008
	#define PMT_CTL_PHY_WAKE_EN		0x00000004
	#define PMT_CTL_WUPS_MASK		0x00000003
	#define PMT_CTL_WUPS_MLT		0x00000003
	#define PMT_CTL_WUPS_MAC		0x00000002
	#define PMT_CTL_WUPS_PHY		0x00000001
#define USB_CFG0			0x080
	#define USB_CFG_BIR			0x00000040
	#define USB_CFG_BCE			0x00000020
#define BURST_CAP			0x090
	#define BURST_CAP_SIZE_MASK_		0x000000FF
#define BULK_IN_DLY			0x094
	#define BULK_IN_DLY_MASK_		0x0000FFFF
#define INT_EP_CTL			0x098
	#define INT_EP_INTEP_ON			0x80000000
	#define INT_STS_EEE_TX_LPI_STRT_EN	0x04000000
	#define INT_STS_EEE_TX_LPI_STOP_EN	0x02000000
	#define INT_STS_EEE_RX_LPI_EN		0x01000000
	#define INT_EP_RDFO_EN			0x00400000
	#define INT_EP_TXE_EN			0x00200000
	#define INT_EP_TX_DIS_EN		0x00080000
	#define INT_EP_RX_DIS_EN		0x00040000
	#define INT_EP_PHY_INT_EN		0x00020000
	#define INT_EP_DP_INT_EN		0x00010000
	#define INT_EP_MAC_ERR_EN		0x00008000
	#define INT_EP_TDFU_EN			0x00004000
	#define INT_EP_TDFO_EN			0x00002000
	#define INT_EP_UTX_FP_EN		0x00001000
	#define INT_EP_GPIO_EN_MASK		0x00000FFF
#define RFE_CTL				0x0B0
	#define RFE_CTL_IGMP_COE		0x00004000
	#define RFE_CTL_ICMP_COE		0x00002000
	#define RFE_CTL_TCPUDP_COE		0x00001000
	#define RFE_CTL_IP_COE			0x00000800
	#define RFE_CTL_BCAST_EN		0x00000400
	#define RFE_CTL_MCAST_EN		0x00000200
	#define RFE_CTL_UCAST_EN		0x00000100
	#define RFE_CTL_VLAN_STRIP		0x00000080
	#define RFE_CTL_DISCARD_UNTAGGED	0x00000040
	#define RFE_CTL_VLAN_FILTER		0x00000020
	#define RFE_CTL_SA_FILTER		0x00000010
	#define RFE_CTL_MCAST_HASH		0x00000008
	#define RFE_CTL_DA_HASH			0x00000004
	#define RFE_CTL_DA_PERFECT		0x00000002
	#define RFE_CTL_RST			0x00000001
#define FCT_RX_CTL			0x0C0
	#define FCT_RX_CTL_EN			0x80000000
	#define FCT_RX_CTL_RST			0x40000000
	#define FCT_RX_CTL_SBF			0x02000000
	#define FCT_RX_CTL_OVFL			0x01000000
	#define FCT_RX_CTL_DROP			0x00800000
	#define FCT_RX_CTL_NOT_EMPTY		0x00400000
	#define FCT_RX_CTL_EMPTY		0x00200000
	#define FCT_RX_CTL_DIS			0x00100000
	#define FCT_RX_CTL_USED_MASK		0x0000FFFF
#define FCT_TX_CTL			0x0C4
	#define FCT_TX_CTL_EN			0x80000000
	#define FCT_TX_CTL_RST			0x40000000
	#define FCT_TX_CTL_NOT_EMPTY		0x00400000
	#define FCT_TX_CTL_EMPTY		0x00200000
	#define FCT_TX_CTL_DIS			0x00100000
	#define FCT_TX_CTL_USED_MASK		0x0000FFFF
#define FCT_RX_FIFO_END			0x0C8
	#define FCT_RX_FIFO_END_MASK		0x0000007F
#define FCT_TX_FIFO_END			0x0CC
	#define FCT_TX_FIFO_END_MASK		0x0000003F
#define FCT_FLOW			0x0D0
#define MAC_CR				0x100
	#define MAC_CR_GMII_EN			0x00080000
	#define MAC_CR_EEE_TX_CLK_STOP_EN	0x00040000
	#define MAC_CR_EEE_EN			0x00020000
	#define MAC_CR_EEE_TLAR_EN		0x00010000
	#define MAC_CR_ADP			0x00002000
	#define MAC_CR_AUTO_DUPLEX		0x00001000
	#define MAC_CR_AUTO_SPEED		0x00000800
	#define MAC_CR_LOOPBACK			0x00000400
	#define MAC_CR_BOLMT_MASK		0x000000C0
	#define MAC_CR_FULL_DUPLEX		0x00000008
	#define MAC_CR_SPEED_MASK		0x00000006
	#define MAC_CR_SPEED_1000		0x00000004
	#define MAC_CR_SPEED_100		0x00000002
	#define MAC_CR_SPEED_10			0x00000000
	#define MAC_CR_RST			0x00000001
#define MAC_RX				0x104
	#define MAC_RX_MAX_SIZE_SHIFT		16
	#define MAC_RX_MAX_SIZE_MASK		0x3FFF0000
	#define MAC_RX_FCS_STRIP		0x00000010
	#define MAC_RX_VLAN_FSE			0x00000004
	#define MAC_RX_RXD			0x00000002
	#define MAC_RX_RXEN			0x00000001
#define MAC_TX				0x108
	#define MAC_TX_BAD_FCS			0x00000004
	#define MAC_TX_TXD			0x00000002
	#define MAC_TX_TXEN			0x00000001
#define FLOW				0x10C
#define RX_ADDRH			0x118
	#define RX_ADDRH_MASK_			0x0000FFFF
#define RX_ADDRL			0x11C
	#define RX_ADDRL_MASK_			0xFFFFFFFF
#define MII_ACC				0x120
	#define MII_ACC_PHY_ADDR_SHIFT		11
	#define MII_ACC_PHY_ADDR_MASK		0x0000F800
		#define PHY_ADDRESS			1
	#define MII_ACC_MIIRINDA_SHIFT		6
	#define MII_ACC_MIIRINDA_MASK		0x000007C0
	#define MII_ACC_MII_READ		0x00000000
	#define MII_ACC_MII_WRITE		0x00000002
	#define MII_ACC_MII_BUSY		0x00000001
#define MII_DATA			0x124
	#define MII_DATA_MASK			0x0000FFFF
#define MAF_BASE			0x400
	#define MAF_HIX				0x00
	#define MAF_LOX				0x04
	#define NUM_OF_MAF			33
	#define MAF_HI_BEGIN			(MAF_BASE + MAF_HIX)
	#define MAF_LO_BEGIN			(MAF_BASE + MAF_LOX)
	#define MAF_HI(index)			(MAF_BASE + (8 * (index)) + (MAF_HIX))
	#define MAF_LO(index)			(MAF_BASE + (8 * (index)) + (MAF_LOX))
	#define MAF_HI_VALID			0x80000000
	#define MAF_HI_TYPE_MASK		0x40000000
	#define MAF_HI_TYPE_SRC			0x40000000
	#define MAF_HI_TYPE_DST			0x00000000
	#define MAF_HI_ADDR_MASK		0x0000FFFF
	#define MAF_LO_ADDR_MASK		0xFFFFFFFF

// TX command A
#define TX_CMD_A_FCS			0x00400000
#define TX_CMD_A_LEN_MASK		0x000FFFFF

// RX command A
#define RX_CMD_A_RED			0x00400000
#define RX_CMD_A_LEN_MASK		0x00003FFF

boolean LAN7800DeviceInitMACAddress (TLAN7800Device *pThis);
boolean LAN7800DeviceInitPHY (TLAN7800Device *pThis);

boolean LAN7800DevicePHYWrite (TLAN7800Device *pThis, u8 uchIndex, u16 usValue);
boolean LAN7800DevicePHYRead (TLAN7800Device *pThis, u8 uchIndex, u16 *pValue);

boolean LAN7800DeviceWaitReg (TLAN7800Device *pThis, u32 nIndex, u32 nMask, u32 nCompare);

boolean LAN7800DeviceReadWriteReg (TLAN7800Device *pThis, u32 nIndex, u32 nOrMask, u32 nAndMask);
boolean LAN7800DeviceWriteReg (TLAN7800Device *pThis, u32 nIndex, u32 nValue);
boolean LAN7800DeviceReadReg (TLAN7800Device *pThis, u32 nIndex, u32 *pValue);

static const char FromLAN7800[] = "lan7800";

// starting at 10, to be sure to not collide with smsc951x driver
static unsigned s_nDeviceNumber = 10;

void LAN7800Device (TLAN7800Device *pThis, TUSBFunction *pFunction)
{
	assert (pThis != 0);

	USBFunctionCopy (&pThis->m_USBFunction, pFunction);
	pThis->m_USBFunction.Configure = LAN7800DeviceConfigure;

	pThis->m_pEndpointBulkIn = 0;
	pThis->m_pEndpointBulkOut = 0;
	pThis->m_pTxBuffer = 0;

	pThis->m_pTxBuffer = malloc (FRAME_BUFFER_SIZE);
	assert (pThis->m_pTxBuffer != 0);
}

void _LAN7800Device (TLAN7800Device *pThis)
{
	assert (pThis != 0);

	if (pThis->m_pTxBuffer != 0)
	{
		free (pThis->m_pTxBuffer);
		pThis->m_pTxBuffer = 0;
	}

	if (pThis->m_pEndpointBulkOut != 0)
	{
		_USBEndpoint (pThis->m_pEndpointBulkOut);
		free (pThis->m_pEndpointBulkOut);
		pThis->m_pEndpointBulkOut = 0;
	}

	if (pThis->m_pEndpointBulkIn != 0)
	{
		_USBEndpoint (pThis->m_pEndpointBulkIn);
		free (pThis->m_pEndpointBulkIn);
		pThis->m_pEndpointBulkIn = 0;
	}

	_USBFunction (&pThis->m_USBFunction);
}

boolean LAN7800DeviceConfigure (TUSBFunction *pUSBFunction)
{
	TLAN7800Device *pThis = (TLAN7800Device *) pUSBFunction;
	assert (pThis != 0);

	// check USB configuration

	if (USBFunctionGetNumEndpoints (&pThis->m_USBFunction) != 3)
	{
		USBFunctionConfigurationError (&pThis->m_USBFunction, FromLAN7800);

		return FALSE;
	}

	const TUSBEndpointDescriptor *pEndpointDesc;
	while ((pEndpointDesc = (TUSBEndpointDescriptor *) USBFunctionGetDescriptor (&pThis->m_USBFunction, DESCRIPTOR_ENDPOINT)) != 0)
	{
		if ((pEndpointDesc->bmAttributes & 0x3F) == 0x02)		// Bulk
		{
			if ((pEndpointDesc->bEndpointAddress & 0x80) == 0x80)	// Input
			{
				if (pThis->m_pEndpointBulkIn != 0)
				{
					USBFunctionConfigurationError (&pThis->m_USBFunction, FromLAN7800);

					return FALSE;
				}

				pThis->m_pEndpointBulkIn = (TUSBEndpoint *) malloc (sizeof (TUSBEndpoint));
				assert (pThis->m_pEndpointBulkIn);
				USBEndpoint2 (pThis->m_pEndpointBulkIn, USBFunctionGetDevice (&pThis->m_USBFunction), pEndpointDesc);
			}
			else							// Output
			{
				if (pThis->m_pEndpointBulkOut != 0)
				{
					USBFunctionConfigurationError (&pThis->m_USBFunction, FromLAN7800);

					return FALSE;
				}

				pThis->m_pEndpointBulkOut = (TUSBEndpoint *) malloc (sizeof (TUSBEndpoint));
				assert (pThis->m_pEndpointBulkOut);
				USBEndpoint2 (pThis->m_pEndpointBulkOut, USBFunctionGetDevice (&pThis->m_USBFunction), pEndpointDesc);
			}
		}
	}

	if (   pThis->m_pEndpointBulkIn  == 0
	    || pThis->m_pEndpointBulkOut == 0)
	{
		USBFunctionConfigurationError (&pThis->m_USBFunction, FromLAN7800);

		return FALSE;
	}

	if (!USBFunctionConfigure (&pThis->m_USBFunction))
	{
		LogWrite (FromLAN7800, LOG_ERROR, "Cannot set interface");

		return FALSE;
	}

	// check chip ID

	u32* nValue = (uint32_t*)AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	if (   !LAN7800DeviceReadReg (pThis, ID_REV, nValue)
	    || (*nValue & ID_REV_CHIP_ID_MASK) >> 16 != ID_REV_CHIP_ID_7800)
	{
		LogWrite (FromLAN7800, LOG_ERROR, "Invalid chip ID (%x)",
					(*nValue & ID_REV_CHIP_ID_MASK) >> 16);

		return FALSE;
	}

	// init hardware

	// HW reset
	if (   !LAN7800DeviceReadWriteReg (pThis, HW_CFG, HW_CFG_LRST, ~0U)
	    || !LAN7800DeviceWaitReg (pThis, HW_CFG, HW_CFG_LRST, 0))
	{
		LogWrite (FromLAN7800, LOG_ERROR, "HW reset failed");

		return FALSE;
	}

	UARTDebugOut("Setting up MAC address \r\n");
	if (!LAN7800DeviceInitMACAddress (pThis))
	{
		LogWrite (FromLAN7800, LOG_ERROR, "Cannot init MAC address");

		return FALSE;
	}

	// for USB high speed
	if (   !LAN7800DeviceWriteReg (pThis, BURST_CAP, DEFAULT_BURST_CAP_SIZE / HS_USB_PKT_SIZE)
	    || !LAN7800DeviceWriteReg (pThis, BULK_IN_DLY, DEFAULT_BULK_IN_DELAY))
	{
		return FALSE;
	}

	// enable the LEDs and SEF mode
	if (!LAN7800DeviceReadWriteReg (pThis, HW_CFG, HW_CFG_LED0_EN | HW_CFG_LED1_EN, ~HW_CFG_MEF))
	{
		return FALSE;
	}

	// enable burst CAP, disable NAK on RX FIFO empty
	if (!LAN7800DeviceReadWriteReg (pThis, USB_CFG0, USB_CFG_BCE, ~USB_CFG_BIR))
	{
		return FALSE;
	}

	// set FIFO sizes
	if (   !LAN7800DeviceWriteReg (pThis, FCT_RX_FIFO_END, (MAX_RX_FIFO_SIZE - 512) / 512)
	    || !LAN7800DeviceWriteReg (pThis, FCT_TX_FIFO_END, (MAX_TX_FIFO_SIZE - 512) / 512))
	{
		return FALSE;
	}

	// interrupt EP is not used
	if (   !LAN7800DeviceWriteReg (pThis, INT_EP_CTL, 0)
	    || !LAN7800DeviceWriteReg (pThis, INT_STS, INT_STS_CLEAR_ALL))
	{
		return FALSE;
	}

	// disable flow control
	if (   !LAN7800DeviceWriteReg (pThis, FLOW, 0)
	    || !LAN7800DeviceWriteReg (pThis, FCT_FLOW, 0))
	{
		return FALSE;
	}

	// init receive filtering engine
	if (!LAN7800DeviceReadWriteReg (pThis, RFE_CTL, RFE_CTL_BCAST_EN | RFE_CTL_DA_PERFECT, ~0U))
	{
		return FALSE;
	}

	// PHY reset
	if (   !LAN7800DeviceReadWriteReg (pThis, PMT_CTL, PMT_CTL_PHY_RST, ~0U)
	    || !LAN7800DeviceWaitReg (pThis, PMT_CTL, PMT_CTL_PHY_RST | PMT_CTL_READY, PMT_CTL_READY))
	{
		LogWrite (FromLAN7800, LOG_ERROR, "PHY reset failed");

		return FALSE;
	}

	// enable AUTO negotiation
	if (!LAN7800DeviceReadWriteReg (pThis, MAC_CR, MAC_CR_AUTO_DUPLEX | MAC_CR_AUTO_SPEED, ~0U))
	{
		return FALSE;
	}

	// enable TX
	if (   !LAN7800DeviceReadWriteReg (pThis, MAC_TX, MAC_TX_TXEN, ~0U)
	    || !LAN7800DeviceReadWriteReg (pThis, FCT_TX_CTL, FCT_TX_CTL_EN, ~0U))
	{
		return FALSE;
	}

	// enable RX
	if (   !LAN7800DeviceReadWriteReg (pThis, MAC_RX,
					   (MAX_RX_FRAME_SIZE << MAC_RX_MAX_SIZE_SHIFT) | MAC_RX_RXEN | MAC_RX_VLAN_FSE,
					   ~MAC_RX_MAX_SIZE_MASK)
	    || !LAN7800DeviceReadWriteReg (pThis, FCT_RX_CTL, FCT_RX_CTL_EN, ~0U))

	{
		return FALSE;
	}

	if (!LAN7800DeviceInitPHY (pThis))
	{
		LogWrite (FromLAN7800, LOG_ERROR, "Cannot init PHY");

		return FALSE;
	}

	TString DeviceName;
	String (&DeviceName);
	StringFormat (&DeviceName, "eth%u", s_nDeviceNumber++);
	DeviceNameServiceAddDevice (DeviceNameServiceGet (), StringGet (&DeviceName), pThis, FALSE);

	_String (&DeviceName);

	return TRUE;
}

TMACAddress *LAN7800DeviceGetMACAddress (TLAN7800Device *pThis)
{
	assert (pThis != 0);

	return &pThis->m_MACAddress;
}

boolean LAN7800DeviceSendFrame (TLAN7800Device *pThis, const void *pBuffer, unsigned nLength)
{
	assert (pThis != 0);

	if (nLength > FRAME_BUFFER_SIZE-TX_HEADER_SIZE)
	{
		return FALSE;
	}

	assert (pThis->m_pTxBuffer != 0);
	assert (pBuffer != 0);
	memcpy (pThis->m_pTxBuffer+TX_HEADER_SIZE, pBuffer, nLength);

	*(u32 *) &pThis->m_pTxBuffer[0] = (nLength & TX_CMD_A_LEN_MASK) | TX_CMD_A_FCS;
	*(u32 *) &pThis->m_pTxBuffer[4] = 0;

	assert (pThis->m_pEndpointBulkOut != 0);
	return DWHCIDeviceTransfer (USBFunctionGetHost (&pThis->m_USBFunction), pThis->m_pEndpointBulkOut,
				    pThis->m_pTxBuffer, nLength+TX_HEADER_SIZE) >= 0;
}

boolean LAN7800DeviceReceiveFrame (TLAN7800Device *pThis, void *pBuffer, unsigned *pResultLength)
{
	assert (pThis != 0);

	assert (pThis->m_pEndpointBulkIn != 0);
	assert (pBuffer != 0);
	TUSBRequest URB;
	USBRequest (&URB, pThis->m_pEndpointBulkIn, pBuffer, FRAME_BUFFER_SIZE, 0);

	if (!DWHCIDeviceSubmitBlockingRequest (USBFunctionGetHost (&pThis->m_USBFunction), &URB))
	{
		_USBRequest (&URB);

		return FALSE;
	}

	u32 nResultLength = USBRequestGetResultLength (&URB);
	if (nResultLength < RX_HEADER_SIZE)
	{
		_USBRequest (&URB);

		return FALSE;
	}

	u32 nRxStatus = *(u32 *) pBuffer;	// RX command A
	if (nRxStatus & RX_CMD_A_RED)
	{
		LogWrite (FromLAN7800, LOG_WARNING, "RX error (status 0x%X)", nRxStatus);

		_USBRequest (&URB);

		return FALSE;
	}

	u32 nFrameLength = nRxStatus & RX_CMD_A_LEN_MASK;
	assert (nFrameLength == nResultLength-RX_HEADER_SIZE);
	assert (nFrameLength > 4);
	if (nFrameLength <= 4)
	{
		_USBRequest (&URB);

		return FALSE;
	}
	nFrameLength -= 4;	// ignore FCS

	//LogWrite (FromLAN7800, LOG_DEBUG, "Frame received (status 0x%X)", nRxStatus);

	memcpy (pBuffer, (u8 *) pBuffer + RX_HEADER_SIZE, nFrameLength); // overwrite RX command A..C

	assert (pResultLength != 0);
	*pResultLength = nFrameLength;

	_USBRequest (&URB);

	return TRUE;
}

boolean LAN7800DeviceIsLinkUp (TLAN7800Device *pThis)
{
	assert (pThis != 0);

	u16 usPHYModeStatus;
	if (!LAN7800DevicePHYRead (pThis, 0x01, &usPHYModeStatus))
	{
		return FALSE;
	}

	return usPHYModeStatus & (1 << 2) ? TRUE : FALSE;
}

boolean LAN7800DeviceInitMACAddress (TLAN7800Device *pThis)
{
	assert (pThis != 0);

	u8 MACAddress[MAC_ADDRESS_SIZE];
	if (!GetMACAddress (MACAddress))
	{
		return FALSE;
	}

	MACAddressSet (&pThis->m_MACAddress, MACAddress);

	u32 nMACAddressLow =    (u32) MACAddress[0]
			     | ((u32) MACAddress[1] << 8)
			     | ((u32) MACAddress[2] << 16)
			     | ((u32) MACAddress[3] << 24);
	u32 nMACAddressHigh =    (u32) MACAddress[4]
			      | ((u32) MACAddress[5] << 8);

	if (   !LAN7800DeviceWriteReg (pThis, RX_ADDRL, nMACAddressLow)
	    || !LAN7800DeviceWriteReg (pThis, RX_ADDRH, nMACAddressHigh))
	{
		return FALSE;
	}

	// set perfect filter entry for own MAC address
	if (   !LAN7800DeviceWriteReg (pThis, MAF_LO (0), nMACAddressLow)
	    || !LAN7800DeviceWriteReg (pThis, MAF_HI (0), nMACAddressHigh | MAF_HI_VALID))
	{
		return FALSE;
	}

	TString MACString;
	String (&MACString);
	MACAddressFormat (&pThis->m_MACAddress, &MACString);
	LogWrite (FromLAN7800, LOG_DEBUG, "MAC address is %s", StringGet (&MACString));

	_String (&MACString);

	return TRUE;
}

boolean LAN7800DeviceInitPHY (TLAN7800Device *pThis)
{
	assert (pThis != 0);

	// Change LED defaults:
	//      orange = link1000/activity
	//      green  = link10/link100/activity
	// led: 0=link/activity          1=link1000/activity
	//      2=link100/activity       3=link10/activity
	//      4=link100/1000/activity  5=link10/1000/activity
	//      6=link10/100/activity    14=off    15=on
	u16 usLEDModeSel;
	if (!LAN7800DevicePHYRead (pThis, 0x1D, &usLEDModeSel))
	{
		return FALSE;
	}

	usLEDModeSel &= ~0xFF;
	usLEDModeSel |= 1 << 0;
	usLEDModeSel |= 6 << 4;

	return LAN7800DevicePHYWrite (pThis, 0x1D, usLEDModeSel);
}

boolean LAN7800DevicePHYWrite (TLAN7800Device *pThis, u8 uchIndex, u16 usValue)
{
	assert (pThis != 0);
	assert (uchIndex <= 30);

	if (   !LAN7800DeviceWaitReg (pThis, MII_ACC, MII_ACC_MII_BUSY, 0)
	    || !LAN7800DeviceWriteReg (pThis, MII_DATA, usValue))
	{
		return FALSE;
	}

	// set the address, index & direction (write to PHY)
	u32 nMIIAccess  = (PHY_ADDRESS << MII_ACC_PHY_ADDR_SHIFT) & MII_ACC_PHY_ADDR_MASK;
	    nMIIAccess |= ((u32) uchIndex << MII_ACC_MIIRINDA_SHIFT) & MII_ACC_MIIRINDA_MASK;
	    nMIIAccess |= MII_ACC_MII_WRITE | MII_ACC_MII_BUSY;

	if (!LAN7800DeviceWriteReg (pThis, MII_ACC, nMIIAccess))
	{
		return FALSE;
	}

	return LAN7800DeviceWaitReg (pThis, MII_ACC, MII_ACC_MII_BUSY, 0);
}

boolean LAN7800DevicePHYRead (TLAN7800Device *pThis, u8 uchIndex, u16 *pValue)
{
	assert (pThis != 0);
	assert (uchIndex <= 30);

	if (!LAN7800DeviceWaitReg (pThis, MII_ACC, MII_ACC_MII_BUSY, 0))
	{
		return FALSE;
	}

	// set the address, index & direction (read from PHY)
	u32 nMIIAccess  = (PHY_ADDRESS << MII_ACC_PHY_ADDR_SHIFT) & MII_ACC_PHY_ADDR_MASK;
	    nMIIAccess |= ((u32) uchIndex << MII_ACC_MIIRINDA_SHIFT) & MII_ACC_MIIRINDA_MASK;
	    nMIIAccess |= MII_ACC_MII_READ | MII_ACC_MII_BUSY;

	u32 nValue;
	if (   !LAN7800DeviceWriteReg (pThis, MII_ACC, nMIIAccess)
	    || !LAN7800DeviceWaitReg (pThis, MII_ACC, MII_ACC_MII_BUSY, 0)
	    || !LAN7800DeviceReadReg (pThis, MII_DATA, &nValue))
	{
		return FALSE;
	}

	assert (pValue != 0);
	*pValue = nValue & MII_DATA_MASK;

	return TRUE;
}

// wait until register 'nIndex' has value 'nCompare' with mask 'nMask' applied,
// check the register each millisecond, timeout after one second
boolean LAN7800DeviceWaitReg (TLAN7800Device *pThis, u32 nIndex, u32 nMask, u32 nCompare)
{
	assert (pThis != 0);

	unsigned nTries = 1000;
	u32 nValue;
	do
	{
		MsDelay (1);

		if (--nTries == 0)
		{
			return FALSE;
		}

		if (!LAN7800DeviceReadReg (pThis, nIndex, &nValue))
		{
			return FALSE;
		}
	}
	while ((nValue & nMask) != nCompare);

	return TRUE;
}

boolean LAN7800DeviceReadWriteReg (TLAN7800Device *pThis, u32 nIndex, u32 nOrMask, u32 nAndMask)
{
	assert (pThis != 0);

	u32 nValue;
	if (!LAN7800DeviceReadReg (pThis, nIndex, &nValue))
	{
		return FALSE;
	}

	nValue &= nAndMask;
	nValue |= nOrMask;

	return LAN7800DeviceWriteReg (pThis, nIndex, nValue);
}

boolean LAN7800DeviceWriteReg (TLAN7800Device *pThis, u32 nIndex, u32 nValue)
{
	assert (pThis != 0);

	if (DWHCIDeviceControlMessage (USBFunctionGetHost (&pThis->m_USBFunction),
				       USBFunctionGetEndpoint0 (&pThis->m_USBFunction),
				       REQUEST_OUT | REQUEST_VENDOR, WRITE_REGISTER,
				       0, nIndex, &nValue, sizeof nValue) < 0)
	{
		LogWrite (FromLAN7800, LOG_WARNING, "Cannot write register 0x%X", nIndex);

		return FALSE;
	}

	return TRUE;
}

boolean LAN7800DeviceReadReg (TLAN7800Device *pThis, u32 nIndex, u32 *pValue)
{
	assert (pThis != 0);

	if (DWHCIDeviceControlMessage (USBFunctionGetHost (&pThis->m_USBFunction),
				       USBFunctionGetEndpoint0 (&pThis->m_USBFunction),
				       REQUEST_IN | REQUEST_VENDOR, READ_REGISTER,
				       0, nIndex, pValue, sizeof *pValue) != (int) sizeof *pValue)
	{
		LogWrite (FromLAN7800, LOG_WARNING, "Cannot read register 0x%X", nIndex);

		return FALSE;
	}

	return TRUE;
}
