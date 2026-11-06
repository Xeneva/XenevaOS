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
volatile unsigned int  __attribute__((aligned(16))) mbox[36];

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

void LBUartInit(){
     register uint32_t r;

    /* initialize UART */
    *UART0_CR = 0;         // turn off UART0

    /* set up clock for consistent divisor values */
    mbox[0] = 9*4;
    mbox[1] = MBOX_REQUEST;
    mbox[2] = MBOX_TAG_SETCLKRATE; // set clock rate
    mbox[3] = 12;
    mbox[4] = 8;
    mbox[5] = 2;           // UART clock
    mbox[6] = 4000000;     // 4Mhz
    mbox[7] = 0;           // clear turbo
    mbox[8] = MBOX_TAG_LAST;
    mbox_call(MBOX_CH_PROP);

    /* map UART0 to GPIO pins */
    r=*GPFSEL1;
    r&=~((7<<12)|(7<<15)); // gpio14, gpio15
    r|=(4<<12)|(4<<15);    // alt0
    *GPFSEL1 = r;
    *GPPUD = 0;            // enable pins 14 and 15
    r=150; while(r--) { asm volatile("nop"); }
    *GPPUDCLK0 = (1<<14)|(1<<15);
    r=150; while(r--) { asm volatile("nop"); }
    *GPPUDCLK0 = 0;        // flush GPIO setup

    *UART0_ICR = 0x7FF;    // clear interrupts

    *UART0_IBRD = 2;       // 115200 baud
    *UART0_FBRD = 0xB;
    *UART0_LCRH = (3 << 5) | (1 << 4);  // 8n1, enable FIFOs
    *UART0_CR = (1<<9) | (1 << 8)| 1;     // enable Tx, Rx, UART
}

/*
 * LBUartPutc -- put a character to UART
 * @param c -- Character to put
 */
void LBUartPutc(char c){
   do{asm volatile("nop");}while(*UART0_FR&0x20);
   *UART0_DR = c;
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


memory_region_t usable_regions[MAX_MEMORY_REGN];
int usable_region_count = 0;

uint64_t nextStart;
void subtract_resv(uint64_t start, uint64_t end, uint64_t resvstart, uint64_t resvend){
   if (resvend < start)
      return;

   start = nextStart;

   if (resvstart >= start){
      usable_regions[usable_region_count].base = start;
      usable_regions[usable_region_count].size = resvstart - start;
      nextStart = usable_regions[usable_region_count].base + usable_regions[usable_region_count].size;
   }
   usable_region_count++;
  
}

extern char phys[];
extern char end[];

uint64_t nextIterationStart;
uint64_t pmmngrdata[1024*1024];

void LBSubtractRegion(uint64_t mem_base, uint64_t mem_end, uint64_t regionStart, 
uint64_t regionEnd){
    if (regionStart < mem_base)
       return;
    if (usable_region_count != 0)
        mem_base = nextIterationStart;
    
    if (regionStart == mem_base)
        nextIterationStart = regionStart + regionEnd;

    if (regionStart > mem_base){
        LBUartPutString("Memory subtracting region : ");
        LBUartPrintHex(mem_base);
        LBUartPutString("Memory region start: ");
        LBUartPrintHex(regionStart);
        usable_regions[usable_region_count].base = mem_base;
        usable_regions[usable_region_count].size = regionStart - mem_base;
         usable_regions[usable_region_count].page_count = usable_regions[usable_region_count].size / 0x1000;
        nextIterationStart = mem_base + (regionEnd - mem_base);
        usable_region_count++;
    }
}


uint64_t mem_base;
uint64_t mem_end;
uint64_t mem_size;

void LBInitializeMemoryMap(uint64_t dtb){
    nextStart = 0;
    //mem_base, mem_size, mem_end;
    LBDTBGetMemorySize(&mem_base,&mem_size);
    mem_end = mem_base + mem_size;

    LBUartPutString("Memory end :");
    LBUartPrintHex(mem_end);
    uint64_t keStart = (uint64_t)phys;
    uint64_t keEnd = (uint64_t)end;
    keEnd = (keEnd + 0x1000 - 1) & ~(0x1000 - 1);
    LBUartPutString("Kernel start : ");
    LBUartPrintHex(keStart);
    LBUartPutString("Kerne End : ");
    LBUartPrintHex(keEnd);
    LBUartPutString("Kernel original end : ");
    LBUartPrintHex((uint64_t)end);

    LBSubtractRegion(mem_base, mem_end, keStart, keEnd);

    uint64_t dtbStart = dtb;
    uint64_t dtbEnd = dtbStart + LBDTBGetTotalSize();
    LBUartPutString("DTB Start : ");
    LBUartPrintHex(dtbStart);
    LBUartPutString("\r\n");
    dtbEnd = (dtbEnd + 0x1000 - 1) & ~(0x1000 - 1);
     LBUartPutString("DTB End: ");
    LBUartPrintHex(dtbEnd);
    LBUartPutString("\r\n");
    LBSubtractRegion(mem_base, mem_end, dtbStart, dtbEnd);

    /* lastly add the remaining region */
    usable_regions[usable_region_count].base = nextIterationStart;
    usable_regions[usable_region_count].size = mem_end - nextIterationStart;
    usable_regions[usable_region_count].page_count = usable_regions[usable_region_count].size / 0x1000;
    usable_region_count++;
}

void LBExitEL2(){
    uint64_t CurrentELPrev;
    asm volatile ("mrs %0, CurrentEL" : "=r"(CurrentELPrev));
    /*LBUartPutString("Current EL: ");
    LBUartPrintInt(CurrentELPrev >> 2);
    LBUartPutc('\r');*/

    uint64_t spsr_el2, sctlr_el1;
    asm volatile ("mrs %0, SPSR_EL2\n" : "=r"(spsr_el2));
    asm volatile ("mrs %0, SCTLR_EL1\n" : "=r"(sctlr_el1));

    asm volatile (
        "ldr x0, =0x1004\n"
        "mrs x1, SCTLR_EL2\n"
        "orr x1, x1, x0\n"
        "msr SCTLR_EL2, x1\n"
        "ldr x0, =0x30d01804\n"
        "msr SCTLR_EL1,x0\n" :::"x0", "x1"
    );
   // LBUartPutString("SCTLR_EL1 set \n");

    asm volatile (
        "ldr x0, =0x80000000\n"
        "msr HCR_EL2,x0\n":::"x0");
    
    asm volatile ("ldr x0, =0x3c5\n"
        "msr SPSR_EL2, x0\n":::"x0");

    asm volatile (
        "mov x0, sp\n"
        "msr SP_EL1, x0\n"
        "adr x0, in_el1\n"
        "msr ELR_EL2, x0\n"
        "eret\n"
        "in_el1:\n":::"x0", "memory", "cc"
    );

   // LBUartPutString("Out of EL2 \n");
    uint64_t CurrentEL;
    asm volatile ("mrs %0, CurrentEL" : "=r"(CurrentEL));
    /*LBUartPutString("Current EL: ");
    LBUartPrintInt(CurrentEL >> 2);
    LBUartPutString("\r\n");*/
}


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
    /* initialize temporary physical memory */
    /* for now, 1MiB page data area is sufficient*/
    /* if the size of xnkrnl increases, for example*/
    /* crosses 1MiB, we need to increase page data area*/
    LBInitializePmmngr(usable_regions, usable_region_count, pmmngrdata, 1024*1024);

    
    /* virtual memory initialize */
    LBPagingInitialize();

    
    /* initialize temporary heap */
    /* initialize ram disk and mount it*/
    /* read the kernel and load it into 
     * memory 
     */
    LBRamdiskInitialize();
    uint64_t stackAddr = 0xFFFFA00000000000;
    for (int i = 0; i < 0x100000 / 4096; i++){
        LBPagingMap(stackAddr + i * 4096, LBPmmngrAlloc());
    }
   
  
    LBUartPutString("Stack mapped \r\n");
    KERNEL_BOOT_INFO info; // = (KERNEL_BOOT_INFO*)addr;
    LittleBootProtocol lbp; // = (LittleBootProtocol*)((uint64_t)info + sizeof(KERNEL_BOOT_INFO));
    
    LBUartPutString("LittleBoot address : \r\n");
    LBUartPrintHex((uint64_t)&info);
    memset(&lbp, 0, sizeof(LittleBootProtocol));
     asm volatile ("dsb ish");
     asm volatile ("isb");
     LBUartPutString("LBP memset really completed \r\n");
    lbp.initrd_start = LBRamdiskGetStartAddress();
    asm volatile ("dsb ish");
    asm volatile ("isb");
     LBUartPutString("LBP memset completed \r\n");
    
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

    LBUartPutString("LBInitrd end \n");
    LBUartPrintHex(lbp.initrd_end);

    LBUartPutString("LittleBoot Protocol prepared \r\n");

    memset(&info, 0, sizeof(KERNEL_BOOT_INFO));
    info.boot_type = BOOT_LITTLEBOOT_ARM64;
    info.allocatedStack = LBPmmngrGetAllocatedPtr();
    info.reserved_mem_count = LBPmmngrGetReservedMemCount();
    info.driver_entry1 = &lbp;
    LBUartPrintHex((uint64_t)&info);

   
    LBRamdiskJumpToKernel(stackAddr, 0x100000, &info);
    while(1);
 }
 