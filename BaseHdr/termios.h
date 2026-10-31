/**
* BSD 2-Clause License
*
* Copyright (c) 2021, Manas Kamal Choudhury
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

#ifndef __TERMIOS_H__
#define __TERMIOS_H__

typedef unsigned char  cc_t;
typedef unsigned int   speed_t;
typedef unsigned int   tcflag_t;

#define NCCS   32

typedef struct _termios_ {
	tcflag_t  c_iflag;   /* input mode flags */
	tcflag_t  c_oflag;   /* output mode flags */
	tcflag_t  c_cflag;   /* control mode flags */
	tcflag_t  c_lflag;   /* local mode flags */
	cc_t      c_line;    /* line discipline */
	cc_t      c_cc[NCCS]; /*control characters */
	speed_t   c_ispeed;   /* input speed */
	speed_t   c_ospeed;   /* output speed */
}Termios;

/* c_cc characters */
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6
#define VSWTC  7
#define VSTART 8
#define VSTOP  9
#define VSUSP  10
#define VEOL   11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

/* c_iflag, input modes */
#define IGNBRK    0000001
#define BRKINT    0000002
#define IGNPAR    0000004
#define PARMRK    0000010
#define INPCK     0000020
#define ISTRIP    0000040
#define INLCR     0000100
#define IGNCR     0000200
#define ICRNL     0000400
#define IUCLC     0001000
#define IXON      0002000
#define IXANY     0004000
#define IXOFF     0010000
#define IMAXBEL   0020000
#define IUTF8     0040000

/* c_oflag, output modes */
#define OPOST    0000001
#define OLCUC    0000002
#define ONLCR    0000004
#define OCRNL    0000010
#define ONOCR    0000020
#define ONLRET   0000040
#define OFILL    0000100
#define OFDEL    0000200
#define NLDLY    0000400
#define NL0      0000000
#define NL1      0000400
#define CRDLY    0003000
#define CR0      0000000
#define CR1      0001000
#define CR2      0002000
#define CR3      0003000
#define TABDLY   0014000
#define TAB0     0000000
#define TAB1     0004000
#define TAB2     0010000
#define TAB3     0014000
#define BSDLY    0020000
#define BS0      0000000
#define BS1      0020000
#define FFDLY    0100000
#define FF0      0000000
#define FF1      0100000
#define VTDLY    0040000
#define VT0      0000000
#define VT1      0040000

/* speed bits */
#define B0       0000000
#define B50      0000001
#define B75      0000002
#define B110     0000003
#define B134     0000004
#define B150     0000005
#define B200     0000006
#define B300     0000007
#define B600     0000010
#define B1200    0000011
#define B1800    0000012
#define B2400    0000013
#define B4800    0000014
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017

/* control modes */
#define CSIZE    0000060
#define CS5      0000000
#define CS6      0000020
#define CS7      0000040
#define CS8      0000060
#define CSTOPB   0000100
#define CREAD    0000200
#define PARENB   0000400
#define PARODD   0001000
#define HUPCL    0002000
#define CLOCAL   0004000

/* local modes */
#define ISIG     0000001
#define ICANON   0000002
#define XCASE    0000004
#define ECHO     0000010
#define ECHOE    0000020
#define ECHOK    0000040
#define ECHONL   0000100
#define NOFLSH   0000200
#define TOSTOP   0000400
#define ECHOCTL  0001000
#define ECHOPRT  0002000
#define ECHOKE   0004000
#define FLUSHO   0010000
#define PENDIN   0040000
#define IEXTEN   0100000

/* t_setattribute bits */
#define TCSANOW    0
#define TCSADRAIN   1
#define TCSAFLUSH   2

/* t_cflush and TCFLSH uses these */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* t_cflow() and TCXONC uses these */
#define TCOOFF   0
#define TCOON    1
#define TCIOFF   2
#define TCION    3



#endif
