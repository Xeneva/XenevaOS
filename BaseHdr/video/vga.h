/* SPDX-License-Identifier: MIT */

#ifndef __LINUX_VIDEO_VGA_H
#define __LINUX_VIDEO_VGA_H

/*
 * DCL <video/vga.h> -- the four VGA register constants bochs.c names, in
 * upstream's spelling (include/video/vga.h:43-76 in linux-7.2.6).
 *
 * mainline's header is the full VGA register map plus vga_r/vga_w/vga_io_r
 * inline accessors for vgacon. The vendored bochs driver uses none of that:
 * its VGA touch is bochs_hw_blank()'s three-port sequence -- write the misc
 * output register, read input status 1 to clear the attribute flip-flop,
 * then write the attribute register. The offsets below are those three
 * registers and the one bit the sequence sets; everything else the header
 * carries is console code this tree does not build.
 *
 * The values are the register *port numbers* on an x86 VGA device. bochs
 * reaches them either through the qemu stdvga's register window (its MMIO
 * BAR offset 0x400+ holds them, which is the path this target takes) or
 * through x86 port I/O (the arm that cannot run here; see <asm/io.h>).
 */

#define VGA_ATT_W   	0x3C0	/* Attribute Controller Data Write Register */
#define VGA_MIS_W   	0x3C2	/* Misc Output Write Register */
#define VGA_IS1_RC  	0x3DA	/* Input Status Register 1 - color emulation */
#define VGA_MIS_COLOR		0x01

#endif /* __LINUX_VIDEO_VGA_H */
