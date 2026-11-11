/**
* BSD 2-Clause License
*
* Copyright (c) 2022-2025, Manas Kamal Choudhury
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

/*
 * LittleBoot -- is a small layer between non-uefi based bootloader and 
 * KernelAA64. Little boot is a flat binary that is loaded by non-uefi 
 * bootloader and follows Linux boot protocol. Little boot setup a very
 * minimal environment for the kernel.asm. It does ---
 * 
 * 1> Parsing DTB and loading the ramdisk
 * 2> Loading the kernel from ramdisk
 * 3> Setup Framebuffer for the kernel
 * 4> Maps the kernel to VA and prepare the KernelBootInfo
 * 5> Jumps to the kernel
 * 
 */

#include "littleboot.h"
#include "dtb.h"
#include "physm.h"
#include "vmmngr.h"
#include "ramdisk.h"
#include "aurora.h"
#include "gpio.h"

#define MBOX_REQUEST 0

#define MBOX_CH_POWER 0
#define MBOX_CH_FB    1
#define MBOX_CH_VUART 2
#define MBOX_CH_VCHIQ 3
#define MBOX_CH_LEDS  4
#define MBOX_CH_BTNS 5
#define MBOX_CH_TOUCH 6
#define MBOX_CH_COUNT 7
#define MBOX_CH_PROP 8

#define MBOX_TAG_GETSERIAL 0x1004
#define MBOX_TAG_SETCLKRATE 0x38002
#define MBOX_TAG_LAST 0

/* mailbox message buffer */
volatile unsigned int  __attribute__((aligned(16))) mbox[64];

#define VIDEOCORE_MBOX  (MMIO_BASE+0x0000B880)
#define MBOX_READ       ((volatile unsigned int*)(VIDEOCORE_MBOX+0x0))
#define MBOX_POLL       ((volatile unsigned int*)(VIDEOCORE_MBOX+0x10))
#define MBOX_SENDER     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x14))
#define MBOX_STATUS     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x18))
#define MBOX_CONFIG     ((volatile unsigned int*)(VIDEOCORE_MBOX+0x1C))
#define MBOX_WRITE      ((volatile unsigned int*)(VIDEOCORE_MBOX+0x20))
#define MBOX_RESPONSE   0x80000000
#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000

/**
 * Make a mailbox call. Returns 0 on failure, non-zero on success
 */
int mbox_call(unsigned char ch)
{
    unsigned int r = (((unsigned int)((unsigned long)&mbox)&~0xF) | (ch&0xF));
    /* wait until we can write to the mailbox */
    do{asm volatile("nop");}while(*MBOX_STATUS & MBOX_FULL);
    /* write the address of our message to the mailbox with channel identifier */
    *MBOX_WRITE = r;
    /* now wait for the response */
    while(1) {
        /* is there a response? */
        do{asm volatile("nop");}while(*MBOX_STATUS & MBOX_EMPTY);
        /* is it a response to our message? */
        if(r == *MBOX_READ)
            /* is it a valid successful response? */
            return mbox[1]==MBOX_RESPONSE;
    }
    return 0;
}

#define MBOX_TAG_GETCLKRATE 0x30002
#define MBOX_CLOCK_CORE 4
#define LB_CONSOLE_BAUD 115200

/*
 * LBUartInit -- serial console on the mini UART (GPIO14/15, ALT5).
 * The PL011 is left to the Bluetooth controller. The mini UART baud
 * rate is derived from the VPU core clock; config.txt's enable_uart=1
 * pins that clock (250 MHz on the Pi 3) so the rate stays stable, and
 * the divisor is computed from what the firmware reports.
 */
void LBUartInit(){
    register uint32_t r;
    uint32_t core = 0;

    /* ask the firmware for the core clock the mini UART runs from */
    mbox[0] = 8*4;
    mbox[1] = MBOX_REQUEST;
    mbox[2] = MBOX_TAG_GETCLKRATE;
    mbox[3] = 8;
    mbox[4] = 0;
    mbox[5] = MBOX_CLOCK_CORE;
    mbox[6] = 0;
    mbox[7] = MBOX_TAG_LAST;
    if (mbox_call(MBOX_CH_PROP) && mbox[5] == MBOX_CLOCK_CORE)
        core = mbox[6];
    if (core == 0)
        core = 250000000;

    *AUX_ENABLES |= 1;     // enable the mini UART block
    *AUX_MU_CNTL = 0;      // transmitter and receiver off while configuring
    *AUX_MU_IER = 0;       // no interrupts
    *AUX_MU_LCR = 3;       // 8 data bits
    *AUX_MU_MCR = 0;
    *AUX_MU_IIR = 0xC6;    // clear both FIFOs
    /* baud = core / (8 * (reg + 1)), rounded to the nearest divisor */
    *AUX_MU_BAUD = (core + 4 * LB_CONSOLE_BAUD) / (8 * LB_CONSOLE_BAUD) - 1;

    /* GPIO14 (TXD1) / GPIO15 (RXD1) -> ALT5, no pulls */
    r=*GPFSEL1;
    r&=~((7<<12)|(7<<15)); // gpio14, gpio15
    r|=(2<<12)|(2<<15);    // alt5
    *GPFSEL1 = r;
    *GPPUD = 0;
    r=150; while(r--) { asm volatile("nop"); }
    *GPPUDCLK0 = (1<<14)|(1<<15);
    r=150; while(r--) { asm volatile("nop"); }
    *GPPUDCLK0 = 0;        // flush GPIO setup

    *AUX_MU_CNTL = 3;      // enable TX and RX
}

/*
 * LBUartPutc -- put a character to UART
 * @param c -- Character to put
 */
void LBUartPutc(char c){
   do{asm volatile("nop");}while(!(*AUX_MU_LSR & AUX_MU_LSR_TX_EMPTY));
   *AUX_MU_IO = (uint32_t)(uint8_t)c;
   asm volatile ("isb\n");
}

/*
 * LBUartPutString -- print a string 
 * to UART
 * @param s -- String to print
 */
void LBUartPutString(const char* s){
    while(*s){
        if (*s =='\n'){
           LBUartPutc('\r');
           LBUartPutc('\n');
          // continue;
        }
        LBUartPutc(*s++);
    }
}

/*
 * LBUartPrintHex -- prints hexadecimal value
 * using serial UART
 * @param val -- value to print
 */
void LBUartPrintHex(uint64_t val){
    LBUartPutc('0');
    LBUartPutc('x');
    for (int i = 60; i >= 0; i -= 4){
        uint8_t nibble = (val >> i) & 0xF;
        char hex = (nibble < 10) ?('0' + nibble) : ('A' + (nibble-10));
        LBUartPutc(hex);
    }
    LBUartPutc('\r');
    LBUartPutc('\n');
}

/*
 * LBUartPrintInt -- prints integer 
 * through UART
 * @param value -- value to print
 */
void LBUartPrintInt(uint64_t value){
    char buffer[12];
    int i = 0; 
    bool is_negative = false;
    if (value == 0){
        LBUartPutc('0');
        return;
    }

    if (value < 0){
        is_negative = true;
        value = -value;
    }

    while (value > 0){
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }

    if (is_negative)
       buffer[i++] = '-';

    while(i--)
       LBUartPutc(buffer[i]);
    
}


/* ------------------------------------------------------------------
 * Physical memory map handed to the kernel
 * ------------------------------------------------------------------ */
memory_region_t usable_regions[MAX_MEMORY_REGN];
int usable_region_count = 0;

typedef struct {
    uint64_t start;
    uint64_t end;
} lb_range_t;

extern char phys[];
extern char end[];

uint64_t pmmngrdata[1024*1024];

uint64_t mem_base;
uint64_t mem_end;
uint64_t mem_size;

#define LB_PAGE_ALIGN_DOWN(x) ((x) & ~0xFFFULL)
#define LB_PAGE_ALIGN_UP(x)   (((x) + 0xFFFULL) & ~0xFFFULL)

/*
 * LBInitializeMemoryMap -- usable RAM = [mem_base, mem_end) minus the
 * low memory below LittleBoot (firmware armstub + spin tables), LittleBoot
 * itself (code, .bss, page tables), the device tree and the initramfs
 */
void LBInitializeMemoryMap(uint64_t dtb){
    LBDTBGetMemorySize(&mem_base,&mem_size);
    mem_end = mem_base + mem_size;

    LBUartPutString("Memory end :");
    LBUartPrintHex(mem_end);

    lb_range_t resv[4];
    int nresv = 0;
    resv[nresv].start = 0;
    resv[nresv++].end = (uint64_t)phys;
    resv[nresv].start = (uint64_t)phys;
    resv[nresv++].end = LB_PAGE_ALIGN_UP((uint64_t)end);
    resv[nresv].start = LB_PAGE_ALIGN_DOWN(dtb);
    resv[nresv++].end = LB_PAGE_ALIGN_UP(dtb + LBDTBGetTotalSize());
    if (LBRamdiskLocate()){
        resv[nresv].start = LB_PAGE_ALIGN_DOWN(LBRamdiskGetStartAddress());
        resv[nresv++].end = LB_PAGE_ALIGN_UP(LBRamdiskGetEndAddress());
    }

    /* sort by start address */
    for (int i = 1; i < nresv; i++){
        lb_range_t key = resv[i];
        int j = i;
        while (j > 0 && resv[j - 1].start > key.start){
            resv[j] = resv[j - 1];
            j--;
        }
        resv[j] = key;
    }

    for (int i = 0; i < nresv; i++){
        LBUartPutString("Reserved : ");
        LBUartPrintHex(resv[i].start);
        LBUartPutString("        -> ");
        LBUartPrintHex(resv[i].end);
    }

    uint64_t cur = mem_base;
    usable_region_count = 0;
    for (int i = 0; i < nresv && usable_region_count < MAX_MEMORY_REGN; i++){
        if (resv[i].end <= cur)
            continue;
        if (resv[i].start >= mem_end)
            break;
        if (resv[i].start > cur){
            usable_regions[usable_region_count].base = cur;
            usable_regions[usable_region_count].size = resv[i].start - cur;
            usable_regions[usable_region_count].page_count = (resv[i].start - cur) / 0x1000;
            usable_region_count++;
        }
        cur = resv[i].end;
    }
    if (cur < mem_end && usable_region_count < MAX_MEMORY_REGN){
        usable_regions[usable_region_count].base = cur;
        usable_regions[usable_region_count].size = mem_end - cur;
        usable_regions[usable_region_count].page_count = (mem_end - cur) / 0x1000;
        usable_region_count++;
    }

    for (int i = 0; i < usable_region_count; i++){
        LBUartPutString("Usable : ");
        LBUartPrintHex(usable_regions[i].base);
        LBUartPutString("  size : ");
        LBUartPrintHex(usable_regions[i].size);
    }
}

/* ------------------------------------------------------------------
 * HDMI framebuffer through the VideoCore property mailbox
 * ------------------------------------------------------------------ */
#define MBOX_TAG_SET_PHYS_WH     0x48003
#define MBOX_TAG_SET_VIRT_WH     0x48004
#define MBOX_TAG_SET_DEPTH       0x48005
#define MBOX_TAG_SET_PIXEL_ORDER 0x48006
#define MBOX_TAG_SET_VIRT_OFFSET 0x48009
#define MBOX_TAG_ALLOCATE_BUFFER 0x40001
#define MBOX_TAG_GET_PITCH       0x40008

#ifndef LB_FB_WIDTH
#define LB_FB_WIDTH  1024
#endif
#ifndef LB_FB_HEIGHT
#define LB_FB_HEIGHT 768
#endif

typedef struct {
    uint64_t address;     /* ARM physical */
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t rgb;         /* 1: bytes R,G,B in memory, 0: B,G,R */
} lb_framebuffer_t;

static lb_framebuffer_t lbfb;

#define MBOX_TAG_GET_PHYS_WH 0x40003

/*
 * LBGetDisplaySize -- size of the mode the firmware drives on HDMI
 * (from the monitor's EDID, or the forced mode in config.txt)
 * @return 1 on success
 */
int LBGetDisplaySize(uint32_t* width, uint32_t* height){
    mbox[0] = 8 * 4;
    mbox[1] = MBOX_REQUEST;
    mbox[2] = MBOX_TAG_GET_PHYS_WH;
    mbox[3] = 8;
    mbox[4] = 0;
    mbox[5] = 0;
    mbox[6] = 0;
    mbox[7] = MBOX_TAG_LAST;
    if (!mbox_call(MBOX_CH_PROP) || mbox[5] == 0 || mbox[6] == 0)
        return 0;
    *width = mbox[5];
    *height = mbox[6];
    return 1;
}

/*
 * LBFramebufferInitialize -- ask the VideoCore for a 32bpp framebuffer
 * scanned out on HDMI. Must run with the MMU off: the mailbox buffer is
 * read back by the GPU straight from memory.
 * @return 1 on success
 */
int LBFramebufferInitialize(uint32_t width, uint32_t height){
    int i = 0;
    mbox[i++] = 0;                       /* size, patched below */
    mbox[i++] = MBOX_REQUEST;

    mbox[i++] = MBOX_TAG_SET_PHYS_WH; mbox[i++] = 8; mbox[i++] = 8;
    int phys_wh = i; mbox[i++] = width; mbox[i++] = height;

    mbox[i++] = MBOX_TAG_SET_VIRT_WH; mbox[i++] = 8; mbox[i++] = 8;
    mbox[i++] = width; mbox[i++] = height;

    mbox[i++] = MBOX_TAG_SET_VIRT_OFFSET; mbox[i++] = 8; mbox[i++] = 8;
    mbox[i++] = 0; mbox[i++] = 0;

    mbox[i++] = MBOX_TAG_SET_DEPTH; mbox[i++] = 4; mbox[i++] = 4;
    int depth = i; mbox[i++] = 32;

    /* 0 = BGR: a 32-bit pixel reads as 0xAARRGGBB, Xeneva's format */
    mbox[i++] = MBOX_TAG_SET_PIXEL_ORDER; mbox[i++] = 4; mbox[i++] = 4;
    int order = i; mbox[i++] = 0;

    mbox[i++] = MBOX_TAG_ALLOCATE_BUFFER; mbox[i++] = 8; mbox[i++] = 8;
    int buf = i; mbox[i++] = 4096; mbox[i++] = 0;

    mbox[i++] = MBOX_TAG_GET_PITCH; mbox[i++] = 4; mbox[i++] = 4;
    int pitch = i; mbox[i++] = 0;

    mbox[i++] = MBOX_TAG_LAST;
    mbox[0] = i * 4;

    if (!mbox_call(MBOX_CH_PROP)){
        LBUartPutString("Framebuffer: mailbox request failed \n");
        return 0;
    }
    if (mbox[depth] != 32 || mbox[buf] == 0){
        LBUartPutString("Framebuffer: no 32bpp buffer \n");
        return 0;
    }

    lbfb.width = mbox[phys_wh];
    lbfb.height = mbox[phys_wh + 1];
    lbfb.rgb = mbox[order];
    /* bus address -> ARM physical */
    lbfb.address = mbox[buf] & 0x3FFFFFFF;
    lbfb.size = mbox[buf + 1];
    lbfb.pitch = mbox[pitch];

    LBUartPutString("Framebuffer : ");
    LBUartPrintHex(lbfb.address);
    LBUartPutString("Framebuffer size : ");
    LBUartPrintHex(lbfb.size);
    LBUartPutString("Framebuffer width : ");
    LBUartPrintInt(lbfb.width);
    LBUartPutString(" height : ");
    LBUartPrintInt(lbfb.height);
    LBUartPutString(" pitch : ");
    LBUartPrintInt(lbfb.pitch);
    LBUartPutString(lbfb.rgb ? " order : RGB\n" : " order : BGR\n");
    return 1;
}

/* ------------------------------------------------------------------
 * EL2 -> EL1
 * ------------------------------------------------------------------ */
void LBExitEL2(){
    uint64_t current_el;
    asm volatile ("mrs %0, CurrentEL" : "=r"(current_el));
    if (((current_el >> 2) & 3) != 2)
        return;

    asm volatile (
        /* EL1 sees the real MIDR/MPIDR */
        "mrs x0, midr_el1\n"
        "msr vpidr_el2, x0\n"
        "mrs x0, mpidr_el1\n"
        "msr vmpidr_el2, x0\n"
        /* EL1/EL0 may use the physical counter and timer, no virtual
         * offset: the kernel ticks on CNTV and measures with CNTPCT */
        "mrs x0, cnthctl_el2\n"
        "orr x0, x0, #3\n"
        "msr cnthctl_el2, x0\n"
        "msr cntvoff_el2, xzr\n"
        /* do not trap FP/SIMD or CP15 accesses to EL2 */
        "mov x0, #0x33ff\n"
        "msr cptr_el2, x0\n"
        "msr hstr_el2, xzr\n"
        /* SCTLR_EL1: RES1 bits, MMU/caches off until LBPagingInitialize */
        "ldr x0, =0x30d00800\n"
        "msr sctlr_el1, x0\n"
        /* HCR_EL2.RW: EL1 is AArch64 */
        "ldr x0, =0x80000000\n"
        "msr hcr_el2, x0\n"
        /* EL1h with DAIF masked */
        "mov x0, #0x3c5\n"
        "msr spsr_el2, x0\n"
        "mov x0, sp\n"
        "msr sp_el1, x0\n"
        "adr x0, 1f\n"
        "msr elr_el2, x0\n"
        "isb\n"
        "eret\n"
        "1:\n" ::: "x0", "memory", "cc"
    );
}

/* the structures the kernel receives; part of LittleBoot's reserved image */
static KERNEL_BOOT_INFO info;
static LittleBootProtocol lbp;

/*
 * LittleBootMain -- little boot main
 * entry point
 */
 void LittleBootMain(uint64_t dtb){
    LBExitEL2();
    LBUartInit();
    LBUartPutString("Little Boot 0.1 \n");
    LBUartPutString("Copyright (C) Manas Kamal Choudhury 2025 \n");
    LBUartPutString("Xeneva --build ARM64 \n");
    asm volatile ("msr daifset,#0x2 \n");
    LBExceptionSetup();
    LBUartPutString("Exception table setuped \n");
    LBDeviceTreeInitialize(dtb);
    LBUartPutString("Device tree initialized \n");
    LBInitializeMemoryMap(dtb);

    /* the mailbox exchange has to happen while the MMU (and with it the
     * D-cache) is still off */
    /* never ask for more than the display shows: a small monitor gets its
     * own size, a big one the LB_FB_WIDTH x LB_FB_HEIGHT cap */
    uint32_t fbw = LB_FB_WIDTH, fbh = LB_FB_HEIGHT, dispw = 0, disph = 0;
    if (LBGetDisplaySize(&dispw, &disph)){
        LBUartPutString("Display : ");
        LBUartPrintInt(dispw);
        LBUartPutString(" x ");
        LBUartPrintInt(disph);
        LBUartPutString("\n");
        if (dispw < fbw) fbw = dispw;
        if (disph < fbh) fbh = disph;
    }
    int have_fb = LBFramebufferInitialize(fbw, fbh);

    /* initialize temporary physical memory */
    /* 1MiB page data area: the free stack holds the lowest 256MiB of
     * usable pages, LittleBoot only needs a few MiB of them */
    LBInitializePmmngr(usable_regions, usable_region_count, pmmngrdata, 1024*1024);

    /* virtual memory initialize */
    LBPagingInitialize(mem_end);

#ifdef LB_POISON_RAM
    /* test aid (make POISON=1): QEMU hands out zeroed RAM, real boards do
     * not. Fill every free page with junk so kernel code that relies on
     * zeroed memory fails in QEMU too */
    for (int r = 0; r < usable_region_count; r++){
        volatile uint64_t* p = (volatile uint64_t*)usable_regions[r].base;
        uint64_t n = usable_regions[r].size / 8;
        for (uint64_t i = 0; i < n; i++)
            p[i] = 0x7000969302000008ULL ^ (i << 12);
    }
    LBUartPutString("Free RAM poisoned \n");
#endif

    /* read the kernel from the initramfs and load it into memory */
    LBRamdiskInitialize();
    uint64_t stackAddr = 0xFFFFA00000000000;
    for (int i = 0; i < 0x100000 / 4096; i++){
        LBPagingMap(stackAddr + i * 4096, LBPmmngrAlloc());
    }

    LBUartPutString("Stack mapped \r\n");

    memset(&lbp, 0, sizeof(LittleBootProtocol));
    lbp.initrd_start = LBRamdiskGetStartAddress();
    lbp.initrd_end = LBRamdiskGetEndAddress();
    lbp.device_tree_base = dtb;
    lbp.device_tree_end = dtb + LBDTBGetTotalSize();
    lbp.device_tree_sz = LBDTBGetTotalSize();

    lbp.littleBootStart = (uint64_t)phys;
    lbp.littleBootEnd = (uint64_t)end;
    lbp.usable_memory_map = &usable_regions;
    lbp.usable_region_count = usable_region_count;
    lbp.physical_start = mem_base;
    lbp.physical_end = mem_end;
    lbp.num_page_count = mem_size / 0x1000;

    LBUartPutString("LittleBoot Protocol prepared \r\n");

    memset(&info, 0, sizeof(KERNEL_BOOT_INFO));
    info.boot_type = BOOT_LITTLEBOOT_ARM64;
    info.physical_direct_map_base = LB_DIRECT_MAP_BASE;
    info.physical_direct_map_size = LB_DIRECT_MAP_SIZE;
    if (have_fb){
        info.graphics_framebuffer = (uint32_t*)lbfb.address;
        info.fb_size = lbfb.size;
        info.XResolution = lbfb.width;
        info.YResolution = lbfb.height;
        info.pixels_per_line = lbfb.pitch / 4;
        if (lbfb.rgb){
            info.redmask = 0x000000FF;
            info.bluemask = 0x00FF0000;
        } else {
            info.redmask = 0x00FF0000;
            info.bluemask = 0x000000FF;
        }
        info.greenmask = 0x0000FF00;
        info.resvmask = 0xFF000000;
    }
    info.driver_entry1 = (uint8_t*)&lbp;
    /* last: anything allocated after this is not reported to the kernel */
    info.allocatedStack = LBPmmngrGetAllocatedPtr();
    info.reserved_mem_count = LBPmmngrGetReservedMemCount();

    LBUartPutString("Kernel boot info : ");
    LBUartPrintHex((uint64_t)&info);

    /* FP/SIMD on for EL1/EL0: CPACR_EL1 is UNKNOWN out of reset on real
     * hardware (QEMU happens to set FPEN for direct kernel boot) and the
     * kernel's compiler may use SIMD registers before it enables them */
    asm volatile ("msr cpacr_el1, %0\n isb" :: "r"(3ULL << 20));

    /* thread id registers are UNKNOWN out of reset on real hardware (QEMU
     * zeroes them); the kernel reads TPIDR_EL1 as its per cpu pointer */
    asm volatile ("msr tpidr_el1, xzr\n msr tpidr_el0, xzr\n msr tpidrro_el0, xzr\n isb"
                  ::: "memory");
    /* generic timers off: their control registers are UNKNOWN out of
     * reset, and the kernel unmasks IRQs briefly before its interrupt
     * controller is set up */
    asm volatile ("msr cntv_ctl_el0, xzr\n msr cntp_ctl_el0, xzr\n isb" ::: "memory");

    LBRamdiskJumpToKernel(stackAddr, 0x100000, &info);
    while(1);
 }
