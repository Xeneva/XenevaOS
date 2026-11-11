/**
* @file vector.c
* 
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

#include <stdint.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Hal/AA64/aa64cpu.h>
#include <aucon.h>
#include <Hal/AA64/gic.h>
#include <Drivers/uart.h>
#include <Drivers/rtcmmio.h>
#include <Hal/AA64/sched.h>
#include <Hal/AA64/aa64percpu.h>
#include <Sync/bkl.h>
#include <Hal/AA64/aa64smp.h>

extern bool AuAA64SyscallFast(AA64Registers* regs);
#include <process.h>
#include <_null.h>
#include <Mm/vmarea.h>
#include <Board/RPI3bp/rpi3bp.h>
#include <Hal/AA64/profile.h>
#include <audrv.h>
#include <timer.h>
#include <fixup.h>

extern uint64_t read_sp();
extern uint64_t read_sp_el1();

void AuDumpRegisters(AA64Thread* thr, AA64Registers* regs) {
	UARTDebugOut("===REGISTER DUMP===\r\n");
	UARTDebugOut("x0: %x x1: %x \r\n", regs->x0, regs->x1);
	UARTDebugOut("x2: %x x3: %x \r\n", regs->x2, regs->x3);
	UARTDebugOut("x4: %x x5: %x \r\n", regs->x4, regs->x5);
	UARTDebugOut("x6: %x x7: %x \r\n", regs->x6, regs->x7);
	UARTDebugOut("x8: %x x9: %x \r\n", regs->x8, regs->x9);
	UARTDebugOut("x10: %x x11: %x \r\n", regs->x10, regs->x11);
	UARTDebugOut("x12: %x x13: %x \r\n", regs->x12, regs->x13);
	UARTDebugOut("x14: %x x15: %x \r\n", regs->x14, regs->x15);
	UARTDebugOut("x16: %x x17: %x \r\n", regs->x16, regs->x17);
	UARTDebugOut("x18: %x x19: %x \r\n", regs->x18, regs->x19);
	UARTDebugOut("x20: %x x21: %x \r\n", regs->x20, regs->x21);
	UARTDebugOut("x22: %x x23: %x \r\n", regs->x22, regs->x23);
	UARTDebugOut("x24: %x x25: %x \r\n", regs->x24, regs->x25);
	UARTDebugOut("x26: %x x27: %x \r\n", regs->x26, regs->x27);
	UARTDebugOut("x28: %x x29: %x \r\n", regs->x28, regs->x29);
	UARTDebugOut("x30: %x \r\n", regs->x30);
	(void)thr;
}

void sync_el1_handler(AA64Registers* regs) {
	uint64_t esr = read_esr_el1();

	/* an exception from EL0 enters the kernel: take the BKL. Exceptions
	 * from EL1 happen inside a kernel context that already holds it.
	 * A syscall that switches away never comes back here on this path:
	 * the scheduler hands its BKL depth over (Hal/sched.c) */
	uint64_t entry_spsr;
	__asm__ volatile("mrs %0, spsr_el1" : "=r"(entry_spsr));
	bool from_el0 = (entry_spsr & 0xC) == 0;
	/* a handful of syscalls have their own locking and skip the BKL */
	if (from_el0 && (esr >> 26) == 0x15 && AuAA64SyscallFast(regs))
		return;
	if (from_el0)
		AuBKLLock();

	if ((esr >> 26) == 0x15) {
		AuAA64SyscallHandler(regs);
		// enable_irqs();
		if (from_el0)
			AuBKLUnlock();
		return;
	}

	uint32_t ec = (esr >> 26) & 0x3F;

	/* A new user thread is entered through AuProcessEntSubThread, which is
	 * kernel code. If that eret landed at EL0, the first MSR DAIFSet in
	 * mask_irqs is trapped here (EC 0x18) and the old path halted the
	 * machine. Promote that entry back to EL1h and retry the instruction.
	 * enter_user then drops to EL0 on purpose. */
	if (ec == 0x18) {
		uint64_t spsr = 0;
		uint64_t elr = 0;
		__asm__ volatile("mrs %0, spsr_el1" : "=r"(spsr));
		__asm__ volatile("mrs %0, elr_el1" : "=r"(elr));
		if ((spsr & 0xF) == 0 && elr >= 0xFFFF000000000000ull) {
			spsr = (spsr & ~0xFFull) | 0x3C5ull;
			__asm__ volatile("msr spsr_el1, %0" ::"r"(spsr));
			/* keep the BKL: kernel code continues at EL1 and drops it
			 * in AuProcessEntSubThread before aa64_enter_user */
			return;
		}
	}

	// AuTextOut("======Synch exception=======\r\n");
	// AuTextOut("FAR: %x, PC: %x \r\n", read_far_el1(), read_elr_el1());

	uint32_t dfsc = esr & 0x3F;

	UARTDebugOut("=======Synchronous Exception occured========= \r\n");
	//AuTextOut("Fault Address (FAR_EL1): %x \r\n", read_far_el1());
	UARTDebugOut("Fault Address (FAR_EL1): %x \r\n", read_far_el1());
	//UARTDebugOut("Fault Address String (FAR_EL1): %s \n", read_far_el1());
	//AuTextOut("Fault Instruction (ELR_EL1): %x \r\n", read_elr_el1());
	UARTDebugOut("Fault Instruction (ELR_EL1): %x \r\n", read_elr_el1());
	// AuTextOut("SP_EL1: %x  \r\n", read_sp());
	// AuTextOut("SP_EL0 : %x \r\n", regs->EL0SP);
	UARTDebugOut("Current SPSel : %d \r\n", read_spsel());
	UARTDebugOut("EC class : %x \r\n", ec);

	/** check if the fault occured on drivers **/
	AuDriver* drv = AuDrvManagerCheckFault(read_elr_el1());
	if (drv) {
		UARTDebugOut("======CRASH in Kernel Driver====== \r\n");
		AuDrvCatchFault(drv, read_elr_el1());
	}

	AA64Thread* currthr = AuGetCurrentThread();
	if (currthr) {
		UARTDebugOut("Current Thread: %s \r\n", currthr->name);
		// AuTextOut("Current Thread: %s \r\n", currthr->name);
		AuDumpRegisters(currthr, regs);
	}

	if (ec == 0x25) {
		// AuTextOut("Stack alignment fault \r\n");
		AuDumpRegisters(currthr, regs);
	}

	AuPmmStats pmm_stats;
	AuPmmngrGetStats(&pmm_stats);
	size_t totalRam = (pmm_stats.managed_pages * 0x1000) / 1024 / 1024;
	size_t usedRam = (pmm_stats.allocated_pages * 0x1000) / 1024 / 1024;
	size_t freeRam = (pmm_stats.free_pages * 0x1000) / 1024 / 1024;
	// AuTextOut("Total RAM : %d MiB, Used RAM : %d MiB , Free RAM : %d MiB\r\n", totalRam, usedRam, freeRam);

	AuProcess* proc = NULL;
	if (currthr) {
		proc = AuProcessFindThread(currthr);
		if (!proc)
			proc = AuProcessFindSubThread(currthr);
	}

	AuVMArea* vma = NULL;
	if (proc)
		vma = AuVMAreaGet(proc, read_elr_el1());
	if (vma) {
		UARTDebugOut("VMA Start -> %x \r\n", vma->start);
		uint64_t offset = (read_elr_el1() - vma->start);
		uint64_t realAddress = 0x600000000 + offset;
		UARTDebugOut("original address of the process -> %x  %x\r\n", read_elr_el1(), realAddress);
	}

	dfsc = esr & 0x3F;

	/* 0x20 insn abort / 0x24 data abort, both "from a lower exception level"
	 * i.e. EL0 -- kill the process instead of hanging the whole machine in
	 * while(1). 0x21/0x25 are aborts taken WITHOUT a change in EL (the
	 * kernel itself faulted) -- leave those falling through to the switch
	 * below and the halt, don't paper over a kernel bug --axiss */
	if (ec == 0x20 || ec == 0x24) {
		UARTDebugOut("[aurora]: EL0 abort, killing the faulting process \r\n");
		if (currthr) {
			if (!proc)
				proc = AuProcessFindSubThread(currthr);
			if (proc)
				AuProcessExit(proc, false);
			if (currthr->state != THREAD_STATE_KILLABLE)
				AuThreadMoveToTrash(currthr);
		}
		AuScheduleThread(regs);
		if (from_el0)
			AuBKLUnlock();
		return;
	}

	if (ec == 0x25) {
		/** data abort */
		uint64_t fixup = __search_fixup_table(read_elr_el1());
		UARTDebugOut("__search_fixup_table: found fixup address : %x \r\n", fixup);
		if (fixup) {
			regs->elr = fixup;
			if (from_el0)
				AuBKLUnlock();
			return;
		}

		//real kernel panic, bug in kernel pointers or somewhere
		UARTDebugOut("[aurora]: kernel failed to recover from kernel data abort \r\n");
		for (;;)
			;
	}

	switch (dfsc) {
	case 0b000000:
		UARTDebugOut("Address size, fault level 0 \r\n");
		break;
	case 0b000001:
		UARTDebugOut("Address Size, fault level 1 \r\n");
		break;
	case 0b000010:
		UARTDebugOut("Address size, fault level 2 \r\n");
		break;
	case 0b000011:
		UARTDebugOut("Address size, fault level 3 \r\n");
		break;
	case 0b000100:
		UARTDebugOut("translation, fault level 0 \r\n");
		break;
	case 0b000101:
		UARTDebugOut("translation, fault level 1 \r\n");
		break;
	case 0b000110:
		UARTDebugOut("translation, fault level 2 \r\n");
		break;
	case 0b000111:
		UARTDebugOut("translation, fault level 3 \r\n");
		break;
	case 0b001001:
		UARTDebugOut("access flag, fault level 1 \r\n");
		break;
	case 0b001010:
		UARTDebugOut("access flag, fault level 2 \r\n");
		break;
	case 0b001011:
		UARTDebugOut("access flag, fault level 3 \r\n");
		break;
	case 0b001101:
		UARTDebugOut("permission fault, level 1 \r\n");
		break;
	case 0b001110:
		UARTDebugOut("permission fault, level 2 \r\n");
		break;
	case 0b001111:
		UARTDebugOut("permission fault, level 3 \r\n");
		break;
	default:
		UARTDebugOut("Unknown fault code \r\n");
		break;
	}
	while (1) {}
}

extern bool aa64_restore_context(AA64Thread* thr);

/* temporary freeze diagnostics: written by sync_el1_wrapper in aa64vector.s
 * before any stack use, so they survive a trashed SP --axiss */
uint64_t dbg_last_esr;
uint64_t dbg_last_far;
uint64_t dbg_last_elr;
uint32_t dbg_fault_count;

bool _userprint = 0;

void setuprint() {
	_userprint = 1;
}

/* ---------------------------------------------------------------------
 * Interrupt work and the BKL
 *
 * Interrupt work (device handlers, timer callbacks) is kernel code and
 * runs under the BKL, but an interrupt handler never spins for it: the
 * owner may be a core waiting, IRQs enabled, for exactly this interrupt.
 * When the BKL is taken elsewhere the work is queued (an SPI is masked at
 * the distributor until then) and the owner core gets an IPI; it runs the
 * queue nested in its own BKL hold, exactly like a single core would have
 * taken the interrupt inside that kernel code.
 * --------------------------------------------------------------------- */
#define IRQ_DEFER_WORDS (1024 / 64)
static volatile uint64_t s_irq_deferred[IRQ_DEFER_WORDS];

static void irqTickWork(void) {
	AuroraTimerRunExpired();
#ifdef __XENEVA_BYPASS_VIRTIO_INPUT__
	/* DCL virtio queues are polled; the input queue is fed from the
	 * tick rather than an MSI because the native input drivers are
	 * not bound on this build. */
	extern void virtio_poll_input_vqs(void);
	virtio_poll_input_vqs();
#endif
}

static void irqRunWork(uint32_t irq) {
	if (irq == 27) {
		irqTickWork();
	} else if (irq == 2) {
		AuPL031RTCIRQHandle();
	} else if (irq >= 32) {
		GICCallSPIHandler(irq);
		GICUnmaskIRQ(irq);
	}
}

/* hand the queue to whoever owns the BKL (or to ourselves if it is free:
 * the SGI is taken as soon as this core unmasks IRQs again) */
static void irqDeferredKick(void) {
	int owner = AuBKLOwnerCPU();
	GICSendSGI(owner >= 0 ? (uint32_t)owner : AA64GetCPUNum(), AA64_IPI_DEFERRED_IRQ);
}

static void irqDefer(uint32_t irq) {
	if (irq >= 32)
		GICMaskIRQ(irq);
	__atomic_fetch_or(&s_irq_deferred[irq / 64], 1ULL << (irq % 64), __ATOMIC_RELEASE);
	irqDeferredKick();
}

/* run interrupt work now if the BKL is ours to take, else defer it */
static void irqRunOrDefer(uint32_t irq) {
	if (AuBKLTryLock()) {
		irqRunWork(irq);
		AuBKLUnlock();
	} else {
		irqDefer(irq);
	}
}

/**
 * @brief AuDeferredIRQRun -- run queued interrupt work, or pass it on to
 * the current BKL owner
 */
static void AuDeferredIRQRun(void) {
	bool any = false;
	for (int w = 0; w < IRQ_DEFER_WORDS; w++)
		if (s_irq_deferred[w])
			any = true;
	if (!any)
		return;
	if (!AuBKLTryLock()) {
		irqDeferredKick();
		return;
	}
	for (int w = 0; w < IRQ_DEFER_WORDS; w++) {
		uint64_t bits = __atomic_exchange_n(&s_irq_deferred[w], 0, __ATOMIC_ACQ_REL);
		while (bits) {
			uint32_t bit = (uint32_t)__builtin_ctzll(bits);
			bits &= bits - 1;
			irqRunWork((uint32_t)w * 64 + bit);
		}
	}
	AuBKLUnlock();
}

#ifdef __TARGET_BOARD_RPI3__
/*
 * Raspberry Pi 3: no GIC. The calling core's local source register says
 * what is pending (see Board/Rpi3bp/rpi3bp.c for the id mapping); each
 * source is handled the way the GIC path below handles the matching id,
 * and the scheduler runs at most once per entry.
 */
static void rpi3_irq_dispatch(AA64Registers* regs) {
	uint32_t src = AuRPI3LocalIRQGetPending();
	AA64PerCPU* cpu = AA64GetPerCPU();
	bool resched = false;

	if (src & INT_SRC_MAILBOX0) {
		uint32_t ipi = AuRPI3IPIReadClear();
		if (ipi & (1u << AA64_IPI_RESCHED)) {
			if (cpu)
				__atomic_store_n(&cpu->ipi_pending, 0, __ATOMIC_RELEASE);
			resched = true;
		}
		if (ipi & ((1u << AA64_IPI_RESCHED) | (1u << AA64_IPI_DEFERRED_IRQ)))
			AuDeferredIRQRun();
	}

	if (src & INT_SRC_GPU) {
		/* level triggered: a deferred line is masked by irqDefer until
		 * its handler has run */
		uint64_t pend = AuRPI3PeripheralIRQPending();
		while (pend) {
			uint32_t bit = (uint32_t)__builtin_ctzll(pend);
			pend &= pend - 1;
			irqRunOrDefer(32 + bit);
		}
	}

	if (src & INT_SRC_CNTVIRQ) {
		resetTimer();
		if (cpu) {
			cpu->tick_count++;
			cpu->sched_tick = 1;
		}
		/* kernel timers and timekeeping stay on the boot core */
		if (!cpu || cpu->cpu_num == 0) {
			AuroraTimerUpdateClock();
			irqRunOrDefer(27);
			AuDeferredIRQRun();
		}
		resched = true;
	}

	if (resched)
		AuScheduleThread(regs);
}
#endif

void irq_el1_handler(AA64Registers* regs) {
#ifdef __TARGET_BOARD_RPI3__
	rpi3_irq_dispatch(regs);
#else
	uint32_t iar = GICReadIAR();
	uint32_t irq = iar & 0x3FF;
	if (irq >= 1020) {
		/* logging here floods serial and livelocks boot on a spurious storm --axiss */
		return;
	}
	if (irq == AA64_IPI_RESCHED) {
		GICSendEOI(iar);
		AA64PerCPU* c = AA64GetPerCPU();
		if (c)
			__atomic_store_n(&c->ipi_pending, 0, __ATOMIC_RELEASE);
		AuDeferredIRQRun();
		AuScheduleThread(regs);
		return;
	}
	if (irq == AA64_IPI_DEFERRED_IRQ) {
		GICSendEOI(iar);
		AuDeferredIRQRun();
		return;
	}
	if (irq == 27) {
		//suspendTimer(); //<--- suspecting this line
		resetTimer();
		// setupTimerIRQ();
		GICSendEOI(iar);
		GICCheckPending(irq);

		AA64PerCPU* cpu = AA64GetPerCPU();
		if (cpu) {
			cpu->tick_count++;
			cpu->sched_tick = 1;
		}
		/* secondary cores only schedule on their tick: kernel timers and
		 * virtio polling stay on the boot core */
		if (cpu && cpu->cpu_num != 0) {
			AuScheduleThread(regs);
			return;
		}

		/* timekeeping has its own lock and runs every tick */
		AuroraTimerUpdateClock();
		/* timer callbacks and polling are kernel code: BKL, or deferred
		 * to its owner (never spun for in interrupt context) */
		irqRunOrDefer(27);
		AuDeferredIRQRun();
		AuScheduleThread(regs);
		return;
	}
	/*else if (irq == 27) {
            AuTextOut("Virtual Timer IRQ fired %d \n", irq);
        }*/
	if (irq == 33) {
		GICSendEOI(iar);
		GICCheckPending(irq);
		return;
	}
	if (irq == UART0_IRQ) {
		GICSendEOI(iar);
		GICCheckPending(irq);
		return;
	}
	if (irq == 2) {
		AuTextOut("PL031 RTC IRQ %d \n", irq);
		irqRunOrDefer(2);
		GICSendEOI(iar);
		GICCheckPending(irq);
		return;
	}
	// GICClearPendingIRQ(irq);

	if (irq >= 32 && irq < 1020) {
		/* masked at the distributor first if deferred, then EOI */
		irqRunOrDefer(irq);
		GICSendEOI(iar);
	}

#endif
}

void fault_el1_handler(AA64Registers* regs) {
	UARTDebugOut("=======Fault Exception occured=========\n");
	UARTDebugOut("Fault Address (FAR_EL1): %x \n", read_far_el1());
	UARTDebugOut("Fault Instruction (ELR_EL1): %x \n", read_elr_el1());
	uint64_t esr = read_esr_el1();

	uint32_t dfsc = esr & 0x3F;

	switch (dfsc) {
	case 0b000000:
		AuTextOut("Address size, fault level 0\n");
		break;
	case 0b000001:
		AuTextOut("Address Size, fault level 1\n");
		break;
	case 0b000010:
		AuTextOut("Address size, fault level 2\n");
		break;
	case 0b000011:
		AuTextOut("Address size, fault level 3 \n");
		break;
	case 0b000100:
		AuTextOut("translation, fault level 0\n");
		break;
	case 0b000101:
		AuTextOut("translation, fault level 1\n");
		break;
	case 0b000110:
		AuTextOut("translation, fault level 2\n");
		break;
	case 0b000111:
		AuTextOut("translation, fault level 3\n");
		break;
	case 0b001001:
		AuTextOut("access flag, fault level 1\n");
		break;
	case 0b001010:
		AuTextOut("access flag, fault level 2\n");
		break;
	case 0b001011:
		AuTextOut("access flag, fault level 3\n");
		break;
	case 0b001101:
		AuTextOut("permission fault, level 1\n");
		break;
	case 0b001110:
		AuTextOut("permission fault, level 2\n");
		break;
	case 0b001111:
		AuTextOut("permission fault, level 3\n");
		break;
	default:
		AuTextOut("Unknown fault code \n");
		break;
	}
	while (1) {}
}

void sync_el0_handler(AA64Registers* regs) {
	uint64_t esr = read_esr_el1();
	UARTDebugOut("SYNC EL0 Hnalder \r\n");
	if ((esr >> 26) == 0x15) {
		AuTextOut("System call trapped %d x30: %x\n", regs->x8, regs->x30);
		AuAA64SyscallHandler(regs);
		return;
	}
}
extern char vectors[];

void AA64InitializeVectorTable() {
	set_vbar_el1((uint64_t)&vectors);
}
