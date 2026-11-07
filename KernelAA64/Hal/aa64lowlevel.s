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

.global __chkstk
__chkstk:
ret

.global store_x0_x7
store_x0_x7:
    str x1, [x0, #0]
    str x2, [x0, #8]
    str x3, [x0, #16]
    str x4, [x0, #24]
    str x5, [x0, #32]
    str x6, [x0, #40]
    str x7, [x0, #48]
    ret

.global store_x2_x7
store_x2_x7:
    stp x2, x3, [x0, #0]
    stp x4, x5, [x0, #16]
    stp x6, x7, [x0, #32]

    stp q0, q1, [x0, #64]
    stp q2, q3, [x0, #96]
    stp q4, q5, [x0, #128]
    stp q6,q7, [x0, #160]
    ret

.global store_x3_x7
store_x3_x7:
    stp x3, x4, [x0, #0]
    stp x5, x6, [x0, #16]
    str x7, [x0, #32]

    stp q0, q1, [x0, #64]
    stp q2, q3, [x0, #96]
    stp q4, q5, [x0, #128]
    stp q6, q7, [x0, #160]
    ret

.global _getCurrentEL
_getCurrentEL:
    mrs x0, CurrentEL
    lsr x0, x0, #2
    ret

.global read_ttbr0_el1
read_ttbr0_el1:
    mrs x0, ttbr0_el1
    ret

.global read_ttbr1_el1
read_ttbr1_el1:
    mrs x0, ttbr1_el1
    ret

.global write_ttbr0_el1
write_ttbr0_el1:
    msr ttbr0_el1, x0
    ret

.global write_ttbr1_el1
write_ttbr1_el1:
    msr ttbr1_el1, x0
    ret

.global write_both_ttbr
write_both_ttbr:
    msr ttbr0_el1, x0
    //msr ttbr1_el1, x0
    isb sy
    dsb ishst
    tlbi vmalle1is 
    dsb ish
    isb 
    ret

.global dsb_sy_barrier
dsb_sy_barrier:
   dsb sy
   isb
   ret

.global read_sctlr_el1
read_sctlr_el1:
    mrs x0, sctlr_el1
    ret

.global write_sctlr_el1
write_sctlr_el1:
    msr sctlr_el1, x0
    ret

.global read_mair_el1
read_mair_el1:
    mrs x0, mair_el1
    ret

.global write_mair_el1
write_mair_el1:
    msr mair_el1, x0
    ret

.global read_tcr_el1
read_tcr_el1:
    mrs x0, tcr_el1
    ret

.global write_tcr_el1
write_tcr_el1:
    msr tcr_el1, x0
    ret

.global dsb_ish
dsb_ish:
     dsb ish
     ret

.global dmb_ish
dmb_ish:
     dmb ish
     ret

.global dmb_sy
dmb_sy:
     dmb sy
     ret

.global isb_flush
isb_flush:
     isb
     ret

.global tlb_flush
tlb_flush:
     dsb ishst
     // TLBI takes VA[55:12], not a byte address. Leave TTL/ASID bits zero.
     ubfx x0, x0, #12, #44
     tlbi vaae1is, x0
     dsb ish
     isb
     ret

.global tlb_flush_vmalle1is
tlb_flush_vmalle1is:
     dsb ishst
     tlbi vmalle1is
     dsb ish
     isb
     ret
      

.global enable_sre
enable_sre:
    mrs x0, ICC_SRE_EL1
    orr x0, x0, #1
    msr ICC_SRE_EL1, x0
    isb 
    ret

.global read_icc_iidr
read_icc_iidr:
   //mrs x0, ICC_IIDR_EL1
   ret

.global set_vbar_el1
set_vbar_el1:
     msr VBAR_EL1, x0
     isb
     ret

.global get_cntpct_el0
get_cntpct_el0:
     mrs x0, CNTPCT_EL0
     ret

.global get_cntvct_el0
get_cntvct_el0:
     mrs x0, CNTVCT_EL0
     ret

.global get_cntfrq_el0
get_cntfrq_el0:
     mrs x0, CNTFRQ_EL0
     ret

.global get_cpacr_el1
get_cpacr_el1:
    mrs x0, CPACR_EL1
    ret

.global get_cntv_ctl_el0
get_cntv_ctl_el0:
    mrs x0, CNTV_CTL_EL0
    ret

.global set_cpacr_el1
set_cpacr_el1:
    msr CPACR_EL1, x0
    ret

.global read_esr_el1
read_esr_el1:
   mrs x0, esr_el1
   ret

.global read_far_el1
read_far_el1:
   mrs x0, far_el1
   ret

.global read_elr_el1
read_elr_el1:
   mrs x0, elr_el1
   ret

.global read_spsr_el1
read_spsr_el1:
   mrs x0, SPSR_EL1
   ret

.global enable_irqs
enable_irqs:
   msr daifclr, #0x2
   dsb sy
   isb
   ret

.global mask_irqs
mask_irqs:
   msr daifset, #0b1111
   dsb sy
   isb
   ret

.global setupTimerIRQ
setupTimerIRQ:
   mrs x0, CNTFRQ_EL0      //mrs x0, CTPCT_EL0
   mov x1, 1000             //mov x1,1000
   udiv x0, x0, x1         //add x0, x0, x1
   msr CNTV_TVAL_EL0, x0   //msr CNTV_CVAL_EL0, x0
   mov x0,1
   msr CNTV_CTL_EL0, x0
   ret

.global suspendTimer
suspendTimer:
   mov x0, 0
   msr CNTV_CTL_EL0, x0
   ret

.global resetTimer
resetTimer:
   mrs x0, CNTFRQ_EL0
   mov x1, 1000
   udiv x0, x0, x1
   msr CNTV_TVAL_EL0,x0
   ret

.global readTimerCtl
readTimerCtl:
   mrs x0,CNTV_CTL_EL0
   ret
.global read_icc_iar1_el1
read_icc_iar1_el1:
   mrs x0, ICC_IAR1_EL1
   ret

.global read_midr
read_midr:
   mrs x0, MIDR_EL1
   ret

.global read_mpidr_el1
read_mpidr_el1:
   mrs x0, mpidr_el1
   ret

.global read_spsel
read_spsel:
   mrs x0, SPSel
   ret

.global read_daif
read_daif:
   mrs x0,daif
   ret

.global restore_daif
restore_daif:
   msr daif, x0
   ret

.extern testFunc
.global aa64_read_tpidr_el0
aa64_read_tpidr_el0:
   mrs x0, tpidr_el0
   ret

.global aa64_set_tpidr_el0
aa64_set_tpidr_el0:
   msr tpidr_el0, x0
   ret

.global aa64_enter_user
aa64_enter_user:
   /* Here, we also mask the IRQ bit for now, else IRQ get fired
    * from CurrentEl with SP_EL0 from vector table, we need to 
    * make the exception handler return to EL0 
    */
  // bic x0, x0, #15
   mov x8, #0x00 //#0x2C0 // #0x80
   msr SPSR_EL1, x8
   msr SP_EL0, x0
   msr ELR_EL1,x1
   mov x0, x2
   eret
  // mov sp, x8
  

.global aa64_svc_test
aa64_svc_test:
   mov x8, 10 
   svc #0

.global aa64_utest
aa64_utest:
  // mov x14,5
   //mov x8, 0
   //svc #0
  // mov x8, x0
  // svc #0
 _hnge:
   //mov x8, 0
  // svc #0
   b _hnge

.global read_x30
read_x30:
   mov x0, x30
   ret

.global read_sp
read_sp:
   mov x0, sp
   ret

.global read_sp_el1
read_sp_el1:
   mrs x0, SP_EL1
   ret

.global sub_rsp
sub_rsp:
   sub sp,sp, #256
   ret

.global dc_ivac
dc_ivac:
   dc ivac, x0
   ret

.global set_syscall_retval
set_syscall_retval:
   mov x27, x0
   ret

 .global modifyx17
 modifyx17:
   mov x17,#1234
   ret

.global enableAlignCheck
enableAlignCheck:
   mrs x0, sctlr_el1
   orr x0, x0, #(1<<3)
   orr x0, x0, #(1<<4)
   msr sctlr_el1, x0
   isb 
   ret


.global data_cache_flush
data_cache_flush:
    cmp x0, #0
    beq data_cache_ret
    dc civac, x0
    dmb sy
    isb
data_cache_ret:
    ret

.global read_ctr_el0
read_ctr_el0:
    mrs x0, ctr_el0
    ret

.global dc_cvac
dc_cvac:
    mov x9, x0
    dc cvac, x9
    ret

.global set_cntp_cval_el0
set_cntp_cval_el0:
    msr cntp_cval_el0, x0
    ret

.global set_cntp_ctl_el0
set_cntp_ctl_el0:
    msr cntp_ctl_el0, x0
    ret

.global _wfi
_wfi:
    wfi
    ret


.global gic_cpu_init
gic_cpu_init:
    mrs x0, ICC_SRE_EL1
    orr x0, x0, #(1<<0)
    msr ICC_SRE_EL1, x0
    isb 
    mrs x0, ICC_SRE_EL1
    tbz x0, #0, .sre_failed

    mov x0, #0xFF
    msr ICC_PMR_EL1, x0
    isb

    mov x0, #0
    msr ICC_BPR1_EL1, x0
    isb

    mrs x0, ICC_CTLR_EL1
    bic x0, x0, #(1<<1)
   // bic x0, x0, #(1<<0)
    msr ICC_CTLR_EL1, x0
    isb

    mov x0, #1
    msr ICC_IGRPEN1_EL1, x0
    isb

    mov x0, #0
    ret
.sre_failed:
    mov x0, #1
    ret
    

.global gic_read_intid
gic_read_intid:
    mrs x0, ICC_IAR1_EL1
    ret

.global gic_icc_eoi
gic_icc_eoi:
    msr ICC_EOIR1_EL1, x0
    isb
    ret


.global aa64_clean_invalidate_dcache
aa64_clean_invalidate_dcache:
    mrs x0, clidr_el1
    and x3, x0, #0x7000000
    lsr x3, x3, #23

    cbz x3, 5f
    mov x10, #0

1:
   add x2, x10, x10, lsr #1
   lsr x1, x0, x2
   and x1, x1, #7
   cmp x1, #2
   b.lt 4f

   msr csselr_el1, x10
   isb
    
   mrs x1, ccsidr_el1
   and x2, x1, #7
   add x2, x2, #4
   mov x4, #0x3ff
   and x4, x4, x1, lsr #3
   clz w5, w4
   mov x7, #0x7fff
   and x7, x7, x1, lsr #13

2:
   mov x9, x4
3:
   lsl x6, x9, x5
   orr x11, x10, x6
   lsl x6, x7, x2
   orr x11, x11, x6
   dc cisw, x11
   subs x7, x7, #1
   b.ge 3b

   subs x9, x9, #1
   b.ge 2b
4:
   add x10, x10, #2
   cmp x3, x10
   b.gt 1b
5:
   dsb sy
   isb
   ret

.global aa64_invalidate_icache
aa64_invalidate_icache:
    ic ialluis
    dsb ish
    isb
    ret
