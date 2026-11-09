/**
* @file aa64smpsoak.s
*
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

// User mode body of the SMP soak test (Hal/smp_soak.c). Copied into a
// user page, so it must be position independent. x0 = this worker's slot:
//   [0] iterations  [8] errors  [16] fast path syscalls  [24] BKL syscalls
//   [32] worker id (distinct per worker and per address space)
// x21..x28 and d8..d15 hold values derived from the id for the whole run.
// Any corruption of them by a context switch, a migration, an FP state
// mix up or an ASID/TLB alias (the slot lives at the same VA in every
// address space) counts as an error. A syscall clobbers x0 and x6 only.

.global aa64_smp_soak_user
.global aa64_smp_soak_user_end
.global aa64_smp_soak_waiter
.global aa64_smp_soak_waker
.global aa64_smp_soak_probe
.balign 16
aa64_smp_soak_user:
    mov x20, x0
    ldr x21, [x20, #32]
1:  // (re)derive the invariants
    add x22, x21, #0x101
    add x23, x21, #0x202
    add x24, x21, #0x303
    add x25, x21, #0x404
    add x26, x21, #0x505
    add x27, x21, #0x606
    add x28, x21, #0x707
    scvtf d8, x22
    scvtf d9, x23
    scvtf d10, x24
    scvtf d11, x25
    scvtf d12, x26
    scvtf d13, x27
    scvtf d14, x28
    scvtf d15, x21
2:  // outer: a burst of integer and FP work in scratch registers
    mov x9, #20000
3:  add x1, x1, x9
    eor x2, x2, x1
    mul x3, x2, x9
    add x4, x4, x3
    scvtf d0, x9
    fadd d1, d1, d0
    fmul d2, d0, d0
    fadd d3, d3, d2
    subs x9, x9, #1
    b.ne 3b
    // verify the invariants
    ldr x10, [x20, #32]
    cmp x10, x21
    b.ne 9f
    add x10, x21, #0x101
    cmp x22, x10
    b.ne 9f
    add x10, x21, #0x202
    cmp x23, x10
    b.ne 9f
    add x10, x21, #0x303
    cmp x24, x10
    b.ne 9f
    add x10, x21, #0x404
    cmp x25, x10
    b.ne 9f
    add x10, x21, #0x505
    cmp x26, x10
    b.ne 9f
    add x10, x21, #0x606
    cmp x27, x10
    b.ne 9f
    add x10, x21, #0x707
    cmp x28, x10
    b.ne 9f
    fcvtzs x10, d8
    cmp x10, x22
    b.ne 9f
    fcvtzs x10, d9
    cmp x10, x23
    b.ne 9f
    fcvtzs x10, d10
    cmp x10, x24
    b.ne 9f
    fcvtzs x10, d11
    cmp x10, x25
    b.ne 9f
    fcvtzs x10, d12
    cmp x10, x26
    b.ne 9f
    fcvtzs x10, d13
    cmp x10, x27
    b.ne 9f
    fcvtzs x10, d14
    cmp x10, x28
    b.ne 9f
    fcvtzs x10, d15
    cmp x10, x21
    b.ne 9f
    // count the iteration
    ldr x10, [x20]
    add x10, x10, #1
    str x10, [x20]
    // a syscall every 16 iterations, rotating over four kinds
    and x11, x10, #15
    cbnz x11, 2b
    and x11, x10, #48
    cmp x11, #0
    b.eq 4f
    cmp x11, #16
    b.eq 5f
    cmp x11, #32
    b.eq 6f
    mov x16, #3             // SYS_GET_THREAD_ID: fast path
    svc #0
    b 7f
4:  mov x16, #23            // SYS_PROCESS_SLEEP 1 ms: fast path, parks the thread
    mov x0, #1
    svc #0
    b 7f
5:  mov x16, #70            // SYS_GET_CURRENT_US: fast path
    svc #0
    b 7f
6:  mov x16, #75            // SYS_GET_NUM_PROCESS_COUNT: takes the BKL
    svc #0
    ldr x10, [x20, #24]
    add x10, x10, #1
    str x10, [x20, #24]
    b 2b
7:  ldr x10, [x20, #16]
    add x10, x10, #1
    str x10, [x20, #16]
    b 2b
9:  // invariant broken: count it and start over
    ldr x10, [x20, #8]
    add x10, x10, #1
    str x10, [x20, #8]
    b 1b

// Pause/wake pair (lost wakeup test). x0 = shared pair page:
//   [0] flag  [8] handoffs  [16] waiter wakes  [24] waiter pauses
//   [32] waiter thread id  [40] signal number
// The waker raises the flag, wakes the waiter and waits for it to take
// the flag. The waiter takes it, or pauses (SYS_PAUSE_THREAD) when it is
// clear. A wakeup lost between the waiter's check and its pause leaves
// both stuck for good: the reporter sees the handoff count stop.
aa64_smp_soak_waiter:
    mov x20, x0
11: ldar x1, [x20]
    cbz x1, 12f
    stlr xzr, [x20]
    ldr x2, [x20, #16]
    add x2, x2, #1
    str x2, [x20, #16]
    b 11b
12: ldr x2, [x20, #24]
    add x2, x2, #1
    str x2, [x20, #24]
    mov x16, #2             // SYS_PAUSE_THREAD
    svc #0
    b 11b

aa64_smp_soak_waker:
    mov x20, x0
21: mov x1, #1
    stlr x1, [x20]
    ldr x0, [x20, #32]
    ldr x1, [x20, #40]
    mov x16, #80            // SYS_SEND_SIGNAL_TO_THREAD
    svc #0
22: ldar x1, [x20]          // wait for the waiter to take it
    cbnz x1, 22b
    ldr x2, [x20, #8]
    add x2, x2, #1
    str x2, [x20, #8]
    mov x9, #20000          // breathe, vary the phase a little
23: subs x9, x9, #1
    b.ne 23b
    b 21b

// Latency probe (core reservation test). x0 = probe slot:
//   [0] iterations  [8] largest gap between two iterations (counter ticks,
//   cleared by the reporter each round)  [16] gaps above [24] threshold
//   [32] core to bind to  [40] go flag  [48] affinity syscall result
// Waits for go, binds itself to its core (SYS_THREAD_SET_AFFINITY), then
// spins reading CNTVCT_EL0. Any interruption (tick, IPI, preemption, a
// context switch) shows up as a large gap. No syscalls in the loop.
aa64_smp_soak_probe:
    mov x20, x0
    add x21, x20, #40
31: ldar x1, [x21]
    cbz x1, 31b
    ldr x0, [x20, #32]
    mov x16, #89            // SYS_THREAD_SET_AFFINITY
    svc #0
    str x0, [x20, #48]
    mrs x22, cntvct_el0
32: mrs x1, cntvct_el0
    sub x2, x1, x22
    mov x22, x1
    ldr x3, [x20, #8]
    cmp x2, x3
    b.ls 33f
    str x2, [x20, #8]
33: ldr x3, [x20, #24]
    cmp x2, x3
    b.ls 34f
    ldr x4, [x20, #16]
    add x4, x4, #1
    str x4, [x20, #16]
34: ldr x4, [x20]
    add x4, x4, #1
    str x4, [x20]
    b 32b
aa64_smp_soak_user_end:
