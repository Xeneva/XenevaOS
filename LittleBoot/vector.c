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

#include "littleboot.h"
#include "vmmngr.h"


void fault_el1_handler(){
    LBUartPutString("Sync Exception \n");
    uint64_t esr;
    asm volatile ("mrs %0, esr_el1" : "=r"(esr));
    uint64_t faultAddr;
    asm volatile ("mrs %0, far_el1" : "=r"(faultAddr));
    uint64_t elr;
    asm volatile ("mrs %0, elr_el1" : "=r"(elr));
    LBUartPutString("Fault address: ");
    LBUartPrintHex(faultAddr);
    LBUartPutString("Instruction: ");
    LBUartPrintHex(elr);
    uint32_t ec = (esr >> 26) & 0x3F;
    uint32_t dfsc = esr & 0x3F;

    if (ec == 0x25)
        LBUartPutString("Stack alignment fault \r\n");

    switch(dfsc){
        case 0b000000: LBUartPutString("address size fault level 0\r\n"); break;
        case 0b000001: LBUartPutString("Address size fault level1\r\n"); break;
        case 0b000010: LBUartPutString("Address size fault level2\r\n"); break;
        case 0b000011: LBUartPutString("Address size fault level 3 \r\n"); break;
        case 0b000100: LBUartPutString("translation fault level 0\r\n"); break;
       case 0b000101: LBUartPutString("translation fault level 1\r\n"); break;
       case 0b000110: LBUartPutString("translation fault level 2\r\n"); break;
       case 0b000111: LBUartPutString("translation fault level 3\r\n"); break;
       case 0b001001: LBUartPutString("access falg fault 1\r\n"); break;
       case 0b001010: LBUartPutString("access falg fault 2\r\n"); break;
       case 0b001011: LBUartPutString("access falg fault 3\r\n"); break;
       case 0b001101: LBUartPutString("permission fault level 1\r\n"); break;
       case 0b001110: LBUartPutString("permission fault level 2\r\n"); break;
       case 0b001111: LBUartPutString("permission fault level 3\r\n"); break;
       default: LBUartPutString("Unknown fault code \r\n"); break;
    }
    LBPageTableWalk(faultAddr);
    while(1);
}

void irq_el1_handler(){
    LBUartPutString("IRQ Exception occured \n");
    while(1);
}

void sync_el1_handler(){
    LBUartPutString("Sync exception 2 \n");
    while(1);
}

extern char vectors[];

/*
 * LBExceptionSetup -- install exception handlers
 */
void LBExceptionSetup(){
    asm volatile ("msr VBAR_EL1, %0\n"::"r"(&vectors));
    LBUartPutString("Little Boot exception handlers installed\n");
}