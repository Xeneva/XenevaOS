/**
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
#ifndef __DWC2_HW_H__
#define __DWC2_HW_H__

/*
 * Synopsys DesignWare HS OTG (DWC2) host registers, as offsets from the
 * controller base, as documented for the Synopsys DWC_otg core (register
 * names as in the databook).
 */

/* BCM2837 (Raspberry Pi 3B/3B+) */
#define DWC2_PHYS_BASE 0x3F980000ULL
/* VideoCore bus address of ARM memory, L2-uncached alias */
#define DWC2_BUS_ALIAS 0xC0000000u

/* core global registers */
#define GOTGCTL	  0x000
#define GAHBCFG	  0x008
#define GUSBCFG	  0x00C
#define GRSTCTL	  0x010
#define GINTSTS	  0x014
#define GINTMSK	  0x018
#define GRXFSIZ	  0x024
#define GNPTXFSIZ 0x028
#define GSNPSID	  0x040
#define GHWCFG1	  0x044
#define GHWCFG2	  0x048
#define GHWCFG3	  0x04C
#define GHWCFG4	  0x050
#define HPTXFSIZ  0x100

/* host mode registers */
#define HCFG	 0x400
#define HFIR	 0x404
#define HFNUM	 0x408
#define HAINT	 0x414
#define HAINTMSK 0x418
#define HPRT0	 0x440

/* host channel n */
#define HCCHAR(n)	(0x500 + 0x20 * (n))
#define HCSPLT(n)	(0x504 + 0x20 * (n))
#define HCINT(n)	(0x508 + 0x20 * (n))
#define HCINTMSK(n) (0x50C + 0x20 * (n))
#define HCTSIZ(n)	(0x510 + 0x20 * (n))
#define HCDMA(n)	(0x514 + 0x20 * (n))

#define PCGCCTL 0xE00

/* GOTGCTL */
#define GOTGCTL_HSTSETHNPEN (1u << 10)

/* GAHBCFG */
#define GAHBCFG_GLBL_INTR_EN	  (1u << 0)
#define GAHBCFG_HBSTLEN_SHIFT	  1
#define GAHBCFG_HBSTLEN_INCR4	  (3u << GAHBCFG_HBSTLEN_SHIFT)
#define GAHBCFG_HBSTLEN_INCR16	  (7u << GAHBCFG_HBSTLEN_SHIFT)
#define GAHBCFG_DMA_EN			  (1u << 5)
/* AHB burst setting used for the BCM2835/7 controller (INCR16) */
#define GAHBCFG_BCM2835			  0x10u

/* GUSBCFG */
#define GUSBCFG_PHYIF16			  (1u << 3)
#define GUSBCFG_ULPI_UTMI_SEL	  (1u << 4)
#define GUSBCFG_SRPCAP			  (1u << 8)
#define GUSBCFG_HNPCAP			  (1u << 9)
#define GUSBCFG_ULPI_FS_LS		  (1u << 17)
#define GUSBCFG_ULPI_CLK_SUSP_M	  (1u << 19)
#define GUSBCFG_ULPI_EXT_VBUS_DRV (1u << 20)
#define GUSBCFG_TERMSELDLPULSE	  (1u << 22)
#define GUSBCFG_FORCEHOSTMODE	  (1u << 29)
#define GUSBCFG_FORCEDEVMODE	  (1u << 30)

/* GRSTCTL */
#define GRSTCTL_CSFTRST		 (1u << 0)
#define GRSTCTL_RXFFLSH		 (1u << 4)
#define GRSTCTL_TXFFLSH		 (1u << 5)
#define GRSTCTL_TXFNUM_SHIFT 6
#define GRSTCTL_TXFNUM_ALL	 0x10u
#define GRSTCTL_AHBIDLE		 (1u << 31)

/* GINTSTS */
#define GINTSTS_CURMODE_HOST (1u << 0)

/* GHWCFG2 */
#define GHWCFG2_ARCH_SHIFT		  3
#define GHWCFG2_ARCH_MASK		  (3u << GHWCFG2_ARCH_SHIFT)
#define GHWCFG2_INT_DMA_ARCH	  2u
#define GHWCFG2_HS_PHY_TYPE_SHIFT 6
#define GHWCFG2_FS_PHY_TYPE_SHIFT 8
#define GHWCFG2_NUM_HOST_CHAN_SHIFT 14
#define GHWCFG2_NUM_HOST_CHAN_MASK (0xFu << GHWCFG2_NUM_HOST_CHAN_SHIFT)
#define GHWCFG2_DYNAMIC_FIFO	  (1u << 19)

/* GHWCFG3 */
#define GHWCFG3_DFIFO_DEPTH_SHIFT 16

/* HCFG */
#define HCFG_FSLSPCLKSEL_MASK	 3u
#define HCFG_FSLSPCLKSEL_30_60_MHZ 0u
#define HCFG_FSLSPCLKSEL_48_MHZ	 1u

/* HFNUM */
#define HFNUM_FRNUM_MASK 0xFFFFu
#define HFNUM_MAX_FRNUM	 0x3FFFu

/* HPRT0: bits 1, 2, 3 and 5 are write-1-to-clear (writing 1 to PRTENA
 * disables the port), so every write masks them out */
#define HPRT0_CONNSTS	  (1u << 0)
#define HPRT0_CONNDET	  (1u << 1)
#define HPRT0_ENA		  (1u << 2)
#define HPRT0_ENACHG	  (1u << 3)
#define HPRT0_OVRCURRACT  (1u << 4)
#define HPRT0_OVRCURRCHG  (1u << 5)
#define HPRT0_RES		  (1u << 6)
#define HPRT0_SUSP		  (1u << 7)
#define HPRT0_RST		  (1u << 8)
#define HPRT0_PWR		  (1u << 12)
#define HPRT0_SPD_SHIFT	  17
#define HPRT0_SPD_MASK	  (3u << HPRT0_SPD_SHIFT)
#define HPRT0_W1C_MASK	  (HPRT0_CONNDET | HPRT0_ENA | HPRT0_ENACHG | HPRT0_OVRCURRCHG)

/* port / device speed as the DWC2 encodes it (HPRT0.PRTSPD) */
#define DWC2_SPEED_HIGH 0
#define DWC2_SPEED_FULL 1
#define DWC2_SPEED_LOW	2

/* HCCHAR */
#define HCCHAR_MPS_MASK		0x7FFu
#define HCCHAR_EPNUM_SHIFT	11
#define HCCHAR_EPDIR		(1u << 15)
#define HCCHAR_LSPDDEV		(1u << 17)
#define HCCHAR_EPTYPE_SHIFT 18
#define HCCHAR_MULTICNT_SHIFT 20
#define HCCHAR_MULTICNT_MASK (3u << HCCHAR_MULTICNT_SHIFT)
#define HCCHAR_DEVADDR_SHIFT 22
#define HCCHAR_ODDFRM		(1u << 29)
#define HCCHAR_CHDIS		(1u << 30)
#define HCCHAR_CHENA		(1u << 31)

/* endpoint types (HCCHAR.EPTYPE) */
#define DWC2_EP_CONTROL 0
#define DWC2_EP_ISOC	1
#define DWC2_EP_BULK	2
#define DWC2_EP_INTR	3

/* HCSPLT */
#define HCSPLT_PRTADDR_SHIFT 0
#define HCSPLT_HUBADDR_SHIFT 7
#define HCSPLT_XACTPOS_SHIFT 14
#define HCSPLT_XACTPOS_ALL	 3u
#define HCSPLT_COMPSPLT		 (1u << 16)
#define HCSPLT_SPLTENA		 (1u << 31)

/* HCINT / HCINTMSK */
#define HCINT_XFERCOMPL	 (1u << 0)
#define HCINT_CHHLTD	 (1u << 1)
#define HCINT_AHBERR	 (1u << 2)
#define HCINT_STALL		 (1u << 3)
#define HCINT_NAK		 (1u << 4)
#define HCINT_ACK		 (1u << 5)
#define HCINT_NYET		 (1u << 6)
#define HCINT_XACTERR	 (1u << 7)
#define HCINT_BBLERR	 (1u << 8)
#define HCINT_FRMOVRUN	 (1u << 9)
#define HCINT_DATATGLERR (1u << 10)

/* HCTSIZ */
#define HCTSIZ_XFERSIZE_MASK 0x7FFFFu
#define HCTSIZ_PKTCNT_SHIFT	 19
#define HCTSIZ_PKTCNT_MAX	 0x3FFu
#define HCTSIZ_PID_SHIFT	 29
#define HCTSIZ_PID_MASK		 (3u << HCTSIZ_PID_SHIFT)

/* data PIDs (HCTSIZ.PID) */
#define DWC2_PID_DATA0 0
#define DWC2_PID_DATA2 1
#define DWC2_PID_DATA1 2
#define DWC2_PID_SETUP 3

#endif
