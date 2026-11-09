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

// PSCI conduits, SMCCC: x0 = function id, x1..x3 = arguments,
// result comes back in x0
.global aa64_psci_call_hvc
aa64_psci_call_hvc:
    hvc #0
    ret

.global aa64_psci_call_smc
aa64_psci_call_smc:
    smc #0
    ret


// aa64_smp_trampoline -- first code a secondary core runs after
// PSCI CPU_ON. AA64SMPInitialize copies everything between
// aa64_smp_trampoline and aa64_smp_trampoline_end into a fresh
// physical page, so this must stay position independent (adr only,
// no literal pools, no absolute symbol references).
//
// entry state : MMU off, caches off, EL2 or EL1
//               x0 = physical address of AA64SMPBootParam
//
// offsets used below come from AA64SMPBootParam in Hal/AA64/aa64smp.h
.global aa64_smp_trampoline
aa64_smp_trampoline:
    msr daifset, #0xf

    mrs x1, CurrentEL
    lsr x1, x1, #2
    cmp x1, #2
    b.ne .Lsmp_in_el1

    // ATF hands us the core in EL2 on real boards (iMX8MP), drop to
    // EL1 the same way XNLDR does for the boot core
    mrs x1, midr_el1
    msr vpidr_el2, x1
    mrs x1, mpidr_el1
    msr vmpidr_el2, x1

    mov x1, #(1 << 31)              // HCR_EL2.RW, EL1 is AArch64
    msr hcr_el2, x1
    mov x1, #0x33ff                 // CPTR_EL2, dont trap FP/SIMD
    msr cptr_el2, x1
    msr hstr_el2, xzr

    mrs x1, cnthctl_el2
    orr x1, x1, #3                  // EL1PCTEN | EL1PCEN
    msr cnthctl_el2, x1
    msr cntvoff_el2, xzr

    // GICv3 system register interface only if the core has one,
    // the virt board runs GICv2 where ICC_* is undefined
    mrs x1, id_aa64pfr0_el1
    ubfx x1, x1, #24, #4
    cbz x1, .Lsmp_no_gicv3
    mrs x1, S3_4_C12_C9_5           // ICC_SRE_EL2
    orr x1, x1, #(1 << 0)           // SRE
    orr x1, x1, #(1 << 3)           // Enable, lower EL may use SRE
    msr S3_4_C12_C9_5, x1
    isb
.Lsmp_no_gicv3:
    mov x1, #0x0800                 // SCTLR_EL1 RES1 bits, MMU off
    movk x1, #0x30d0, lsl #16
    msr sctlr_el1, x1

    mov x1, #0x3c5                  // EL1h, DAIF masked
    msr spsr_el2, x1
    adr x1, .Lsmp_in_el1
    msr elr_el2, x1
    isb
    eret

.Lsmp_in_el1:
    // known state before touching translation registers
    mov x1, #0x0800
    movk x1, #0x30d0, lsl #16
    msr sctlr_el1, x1
    isb

    // FPU/NEON on, kernel C code is free to use q registers
    mov x1, #(3 << 20)
    msr cpacr_el1, x1

    ldr x1, [x0, #64]
    msr vbar_el1, x1
    ldr x1, [x0, #48]
    msr mair_el1, x1
    ldr x1, [x0, #40]
    msr tcr_el1, x1
    ldr x1, [x0, #24]
    msr ttbr0_el1, x1
    ldr x1, [x0, #32]
    msr ttbr1_el1, x1
    isb

    tlbi vmalle1
    ic iallu
    dsb nsh
    isb

    // pull everything we need into registers while the MMU is still
    // off, once it is on this page is reached only through the
    // private identity map in idmap_ttbr0 and we leave it right away
    ldr x1, [x0, #8]
    mov sp, x1
    ldr x2, [x0, #0]
    ldr x3, [x0, #56]
    ldr x0, [x0, #16]
    mov x29, xzr
    mov x30, xzr

    msr sctlr_el1, x3
    isb
    br x2

.global aa64_smp_trampoline_end
aa64_smp_trampoline_end:
