
.globl start    // Make 'start' globally visible (like PUBLIC)
.extern printName
.extern el2_exit

start:
    mov x0, #100
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

.global read_ttbr0_el2
read_ttbr0_el2:
    mrs x0, ttbr0_el2
    ret

.global write_ttbr1_el1
write_ttbr1_el1:
    msr ttbr1_el1, x0
    ret

.global read_spsr_el2
read_spsr_el2:
    mrs x0,SPSR_EL2
    ret

.global write_spsr_el2
write_spsr_el2:
    msr SPSR_EL2,x0
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

.global read_tcr_el2
read_tcr_el2:
    mrs x0, tcr_el2
    ret


.global dsb_ish
dsb_ish:
     dsb ish
     ret

.global isb_flush
isb_flush:
     isb
     ret

.global tlb_flush
tlb_flush:
     dsb ishst
     tlbi vae1is, x0
     dsb ish
     isb
     ret

.global tlb_flush_all
tlb_flush_all:
    isb sy
    dsb ishst
    tlbi vmalle1is 
    dsb ish
    isb 
    ret

.global read_esr_el1
read_esr_el1:
     mrs x0, ESR_EL1
     ret

.global read_far_el1
read_far_el1:
    mrs x0, FAR_EL1
    ret

.global read_elr_el1
read_elr_el1:
    mrs x0, ELR_EL1
    ret

.global write_vbar_el1
write_vbar_el1:
    msr vbar_el1, x0
    ret

.global dc_cvau
dc_cvau:
   dc cvau,x0
   ret

.global ic_ivau
ic_ivau:
   ic ivau, x0
   ret

.global callKernel
callKernel:
    //x0 -- param1
    //x1 -- stack
    //x2 -- stacksz
    //x3 -- entry
    /* xnldr.cpp maps a dedicated 1MB stack at `stack` before calling here,
     * but this switch was never wired up -- the kernel ran on whatever
     * stack UEFI left behind instead, which is why boot_self_test's large
     * locals fault once something finally needs real stack depth --axiss */
    add x1, x1, x2
    bic x1, x1, #15 //align it to 16-byte boundary
    mov sp, x1

    stp x29, x30, [sp, #-16]!
    mov x29, sp

    sub sp, sp, #32
    br x3
_hang:
    b _hang


.global prepare_el2_exit_phase1
prepare_el2_exit_phase1:
    mrs x0, CLIDR_EL1
    and x3, x0, #0x07000000
    lsr x3, x3, #23
    cbz x3, .flush_done
    mov x10, #0
.flush_level:
    add x2, x10, x10, lsr #1
    lsr x1, x0, x2
    and x1, x1, #7
    cmp x1, #2
    blt  .skip_level
    msr CSSELR_EL1, x10
    isb
    mrs x1, CCSIDR_EL1
    and x2, x1, #7
    add x2, x2, #4
    ubfx x4, x1, #3, #10
    clz w5, w4
    ubfx x6, x1, #13, #15
.flush_set:
    mov x7, x4
.flush_way:
    lsl x9, x7, x5
    orr x9, x10, x9
    lsl x8, x6, x2  
    orr x9, x9, x8 
    dc cisw, x9
    subs x7, x7, #1
    bge .flush_way 
    subs x6, x6, #1
    bge .flush_set 
.skip_level:
    add x10, x10, #2
    cmp x10, x3
    blt .flush_level
.flush_done:
    dsb sy 
    isb

    ldr x0, =0x1004           //ldr x0, =0x1005
    mrs x1, SCTLR_EL2            // mrs x1, SCTLR_EL2
    orr x1,x1, x0          //bic x1, x1, #(1<<0)
    msr SCTLR_EL2,x1          //bic x1, x1, #(1<<2)
                //bic x1, x1, #(1<<12)
                // orr x1,x1,x0
    msr SCTLR_EL2,x1
    isb
    ldr x0, =0x30d01804  //0x30500800
    msr SCTLR_EL1,x0
    isb
    ret

.global prepare_el2_exit_phase2
prepare_el2_exit_phase2:
    mrs x0, ICC_SRE_EL2
    orr x0, x0, #(1<<0)
    orr x0, x0, #(1<<3)
    msr ICC_SRE_EL2, x0
    isb
    mrs x0, ICC_SRE_EL1
    orr x0, x0, #(1<<0)
    msr ICC_SRE_EL1, x0
    isb
    mrs x0, CNTHCTL_EL2
    orr x0, x0, #(1<<1)
    orr x0, x0, #(1<<0)
    bic x0, x0, #(1<<2)
    bic x0, x0, #(1<<3)
    msr CNTHCTL_EL2, x0
    isb
    msr CNTVOFF_EL2, xzr
    isb
    ldr x0, =0x80000000  // 0x80000038 //
    msr HCR_EL2, x0
    ldr x0, =0x3C5
    msr SPSR_EL2, x0
    mov x0, sp
    msr SP_EL1, x0
    adr x0, in_el1
    msr ELR_EL2, x0
    eret
in_el1:
    ret



