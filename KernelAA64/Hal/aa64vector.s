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

.extern sync_el1_handler
.extern fault_el1_handler
.extern irq_el1_handler
.extern sync_el0_handler
.extern PrintX0
.extern syscall
.extern dbg_last_esr
.extern dbg_last_far
.extern dbg_last_elr
.extern dbg_fault_count

/* The GPR frame is pushed first. These macros then reserve 528 bytes below
 * it, making the C-visible AA64Registers pointer fp_frame + 528. Saving here,
 * before the first BL, prevents interrupt handlers from corrupting user or
 * kernel NEON state. x9 is safe scratch because its interrupted value is
 * already in the GPR frame. */
.macro save_fp_exception_frame
   sub sp, sp, #528
   stp q0, q1, [sp, #(0*32)]
   stp q2, q3, [sp, #(1*32)]
   stp q4, q5, [sp, #(2*32)]
   stp q6, q7, [sp, #(3*32)]
   stp q8, q9, [sp, #(4*32)]
   stp q10, q11, [sp, #(5*32)]
   stp q12, q13, [sp, #(6*32)]
   stp q14, q15, [sp, #(7*32)]
   stp q16, q17, [sp, #(8*32)]
   stp q18, q19, [sp, #(9*32)]
   stp q20, q21, [sp, #(10*32)]
   stp q22, q23, [sp, #(11*32)]
   stp q24, q25, [sp, #(12*32)]
   stp q26, q27, [sp, #(13*32)]
   stp q28, q29, [sp, #(14*32)]
   stp q30, q31, [sp, #(15*32)]
   mrs x9, fpcr
   str x9, [sp, #512]
   mrs x9, fpsr
   str x9, [sp, #520]
.endm

.macro restore_fp_exception_frame
   ldr x9, [sp, #512]
   msr fpcr, x9
   ldr x9, [sp, #520]
   msr fpsr, x9
   ldp q0, q1, [sp, #(0*32)]
   ldp q2, q3, [sp, #(1*32)]
   ldp q4, q5, [sp, #(2*32)]
   ldp q6, q7, [sp, #(3*32)]
   ldp q8, q9, [sp, #(4*32)]
   ldp q10, q11, [sp, #(5*32)]
   ldp q12, q13, [sp, #(6*32)]
   ldp q14, q15, [sp, #(7*32)]
   ldp q16, q17, [sp, #(8*32)]
   ldp q18, q19, [sp, #(9*32)]
   ldp q20, q21, [sp, #(10*32)]
   ldp q22, q23, [sp, #(11*32)]
   ldp q24, q25, [sp, #(12*32)]
   ldp q26, q27, [sp, #(13*32)]
   ldp q28, q29, [sp, #(14*32)]
   ldp q30, q31, [sp, #(15*32)]
   add sp, sp, #528
.endm

sync_el1_wrapper:
   //mov x9, sp
  // bic x9, x9, #15
   //mov sp, x9
   /* temporary freeze diagnostics: stash fault syndrome before touching
    * the stack, so a trashed SP still leaves evidence --axiss */
   //mrs x9, ESR_EL1
   //mrs x10, FAR_EL1
   //mrs x11, ELR_EL1
   //adrp x12, dbg_last_esr
   //str x9, [x12, #:lo12:dbg_last_esr]
   //adrp x12, dbg_last_far
   //str x10, [x12, #:lo12:dbg_last_far]
   //adrp x12, dbg_last_elr
   //str x11, [x12, #:lo12:dbg_last_elr]
   //adrp x12, dbg_fault_count
   //ldr w13, [x12, #:lo12:dbg_fault_count]
   //add w13, w13, #1
   //str w13, [x12, #:lo12:dbg_fault_count]
   stp x0, x1, [sp, #-16]!
   stp x2, x3, [sp, #-16]!
   stp x4, x5, [sp, #-16]!
   stp x6, x7, [sp, #-16]!
   stp x8, x9, [sp, #-16]!
   stp x10, x11, [sp, #-16]!
   stp x12, x13, [sp, #-16]!
   stp x14, x15, [sp, #-16]!
   stp x16, x17, [sp, #-16]!
   stp x18, x19, [sp, #-16]!
   stp x20, x21, [sp, #-16]!
   stp x22, x23, [sp, #-16]!
   stp x24, x25, [sp, #-16]!
   stp x26, x27, [sp, #-16]!
   stp x28, x29, [sp, #-16]!
   mrs x0, SP_EL0
   stp x30, x0, [sp, #-16]!
   mrs x0, ELR_EL1
   mrs x1, SPSR_EL1
   stp x0, x1, [sp, #-16]!
   save_fp_exception_frame
   add x0, sp, #528 //528

   bl sync_el1_handler
   restore_fp_exception_frame
   ldp x0, x1, [sp], #16
   msr ELR_EL1, x0
   msr SPSR_EL1, x1
   ldp x30, x0,  [sp],  #16
   msr SP_EL0, x0
   ldp x28, x29, [sp] ,#16
   ldp x26, x27, [sp], #16
   ldp x24, x25, [sp], #16
   ldp x22, x23, [sp], #16
   ldp x20, x21, [sp], #16
   ldp x18, x19, [sp], #16
   ldp x16, x17, [sp], #16
   ldp x14, x15, [sp], #16
   ldp x12, x13, [sp], #16
   ldp x10, x11, [sp], #16
   ldp x8, x9, [sp], #16
   ldp x6, x7, [sp], #16
   ldp x4, x5, [sp], #16
   ldp x2, x3, [sp], #16
   ldp x0, x1, [sp], #16
   eret

.global irq_el1_wrapper
irq_el1_wrapper:
  // mov x9, sp
   //bic x9, x9, #15
   //mov sp, x9
   stp x0, x1, [sp, #-16]!
   stp x2, x3, [sp, #-16]!
   stp x4, x5, [sp, #-16]!
   stp x6, x7, [sp, #-16]!
   stp x8, x9, [sp, #-16]!
   stp x10, x11, [sp, #-16]!
   stp x12, x13, [sp, #-16]!
   stp x14, x15, [sp, #-16]!
   stp x16, x17, [sp, #-16]!
   stp x18, x19, [sp, #-16]!
   stp x20, x21, [sp, #-16]!
   stp x22, x23, [sp, #-16]!
   stp x24, x25, [sp, #-16]!
   stp x26, x27, [sp, #-16]!
   stp x28, x29, [sp, #-16]!
   mrs x0, SP_EL0    //Using SP_EL0 because SPSR_EL1 is set to use this 
   stp x30, x0, [sp, #-16]!
   mrs x0, ELR_EL1
   mrs x1, SPSR_EL1
   stp x0, x1, [sp, #-16]!
   save_fp_exception_frame
   add x0, sp, #528 //528
   bl irq_el1_handler
   restore_fp_exception_frame
   ldp x0, x1, [sp], #16
   msr ELR_EL1, x0
   msr SPSR_EL1, x1
   ldp x30, x0, [sp], #16
   msr SP_EL0, x0
   ldp x28, x29, [sp], #16
   ldp x26, x27, [sp], #16
   ldp x24, x25, [sp], #16
   ldp x22, x23, [sp], #16
   ldp x20, x21, [sp], #16
   ldp x18, x19, [sp], #16
   ldp x16, x17, [sp], #16
   ldp x14, x15, [sp], #16
   ldp x12, x13, [sp], #16
   ldp x10, x11, [sp], #16
   ldp x8, x9, [sp], #16
   ldp x6, x7, [sp], #16
   ldp x4, x5, [sp], #16
   ldp x2, x3, [sp], #16
   ldp x0, x1, [sp], #16
   eret

.global sync_el0_wrapper
sync_el0_wrapper:
   //mov x9, sp
   //bic x9, x9, #15
   //mov sp, x9
   stp x0, x1, [sp, #-16]!
   stp x2, x3, [sp, #-16]!
   stp x4, x5, [sp, #-16]!
   stp x6, x7, [sp, #-16]!
   stp x8, x9, [sp, #-16]!
   stp x10, x11, [sp, #-16]!
   stp x12, x13, [sp, #-16]!
   stp x14, x15, [sp, #-16]!
   stp x16, x17, [sp, #-16]!
   stp x18, x19, [sp, #-16]!
   stp x20, x21, [sp, #-16]!
   stp x22, x23, [sp, #-16]!
   stp x24, x25, [sp, #-16]!
   stp x26, x27, [sp, #-16]!
   stp x28, x29, [sp, #-16]!
   mrs x0, SP_EL0
   stp x30, x0, [sp, #-16]!
   mrs x0, ELR_EL1
   mrs x1, SPSR_EL1
   stp x0, x1, [sp, #-16]!
   save_fp_exception_frame
   add x0, sp, #528  //528
   bl sync_el1_handler

   mrs x0, SPSR_EL1
   and w0, w0, #0x200000
   cbz w0, _sync_cont
   mrs x0, MDSCR_EL1
   orr x0, x0, #1
   msr MDSCR_EL1, x0
   _sync_cont: 

   restore_fp_exception_frame
   ldp x0, x1, [sp], #16
   msr ELR_EL1, x0
   msr SPSR_EL1, x1
   ldp x30, x0, [sp], #16
   msr SP_EL0, x0
   //mov x0, 0x2c0
  // msr SPSR_EL1, x0
   ldp x28, x29, [sp], #16
   ldp x26, x27, [sp], #16
   ldp x24, x25, [sp], #16
   ldp x22, x23, [sp], #16
   ldp x20, x21, [sp], #16
   ldp x18, x19, [sp], #16
   ldp x16, x17, [sp], #16
   ldp x14, x15, [sp], #16
   ldp x12, x13, [sp], #16
   ldp x10, x11, [sp], #16
   ldp x8, x9, [sp], #16
   ldp x6, x7, [sp], #16
   ldp x4, x5, [sp], #16
   ldp x2, x3, [sp], #16
   ldp x0, x1, [sp], #16
   eret

.global irq_el0_wrapper
irq_el0_wrapper:
   //mov x9, sp
  //bic x9, x9, #15
   //mov sp, x9
   stp x0, x1, [sp, #-16]!
   stp x2, x3, [sp, #-16]!
   stp x4, x5, [sp, #-16]!
   stp x6, x7, [sp, #-16]!
   stp x8, x9, [sp, #-16]!
   stp x10, x11, [sp, #-16]!
   stp x12, x13, [sp, #-16]!
   stp x14, x15, [sp, #-16]!
   stp x16, x17, [sp, #-16]!
   stp x18, x19, [sp, #-16]!
   stp x20, x21, [sp, #-16]!
   stp x22, x23, [sp, #-16]!
   stp x24, x25, [sp, #-16]!
   stp x26, x27, [sp, #-16]!
   stp x28, x29, [sp, #-16]!
   mrs x0, SP_EL0    //Using SP_EL0 because SPSR_EL1 is set to use this 
   stp x30, x0, [sp, #-16]!
   mrs x0, ELR_EL1
   mrs x1, SPSR_EL1
   stp x0, x1, [sp, #-16]!
   save_fp_exception_frame
   add x0, sp, #528  //528
   bl irq_el1_handler
    mrs x0, SPSR_EL1
   and w0, w0, #0x200000
   cbz w0, _irq_cont
   mrs x0, MDSCR_EL1
   orr x0, x0, #1
   msr MDSCR_EL1, x0
   _irq_cont: 

   restore_fp_exception_frame
   ldp x0, x1, [sp], #16
   msr ELR_EL1, x0
   msr SPSR_EL1, x1
   ldp x30, x0, [sp], #16
   msr SP_EL0, x0
   //mov x0, 0x2c0
   //msr SPSR_EL1, x0
   ldp x28, x29, [sp], #16
   ldp x26, x27, [sp], #16
   ldp x24, x25, [sp], #16
   ldp x22, x23, [sp], #16
   ldp x20, x21, [sp], #16
   ldp x18, x19, [sp], #16
   ldp x16, x17, [sp], #16
   ldp x14, x15, [sp], #16
   ldp x12, x13, [sp], #16
   ldp x10, x11, [sp], #16
   ldp x8, x9, [sp], #16
   ldp x6, x7, [sp], #16
   ldp x4, x5, [sp], #16
   ldp x2, x3, [sp], #16
   ldp x0, x1, [sp], #16
   eret


/* define vectors */
.global vectors
.balign 0x800
/*
 * Current EL with SP_EL0
 */
vectors:
   b sync_el0_wrapper  //sync el1t
.balign 0x80
   b irq_el0_wrapper  //irq
.balign 0x80
   b .  //fiq
.balign 0x80 
   b .   //serror

/*
 * Current EL with SP_EL1
 */
.balign 0x80
   b sync_el1_wrapper  //sync el1
.balign 0x80
   b irq_el1_wrapper   //irq
.balign 0x80
   b irq_el1_wrapper                 //FIQ 
.balign 0x80
   b .                 //SError


//==========================
// Lower EL --aarch64
//==========================
.balign 0x80
   b sync_el0_wrapper   //sync
.balign 0x80
   b irq_el0_wrapper    //IRQ  
.balign 0x80
   b . //irq_el0_wrapper //FIQ
.balign 0x80
   b .

//===========================
// Lower EL aarch32
//===========================
.balign 0x80
   b .                 //sync
.balign 0x80
   b .                 //IRQ
.balign 0x80
   b .                 //FIQ
.balign 0x80
   b .                 //SError
