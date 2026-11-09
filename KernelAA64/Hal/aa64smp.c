/**
* @file aa64smp.c
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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <_null.h>
#include <aurora.h>
#include <dtb.h>
#include <aucon.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64smp.h>
#include <Hal/AA64/aa64percpu.h>
#include <Hal/AA64/gic.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>

/*
 * Secondary core bring up, first milestone: every core described
 * under /cpus with enable-method = "psci" is started with CPU_ON,
 * gets the boot core's translation regime, reaches higher half C,
 * prints itself and parks. No scheduler, no GIC CPU interface yet.
 *
 * Boot page layout (one physical page, entered by physical address
 * with the MMU off):
 *
 *   0x000 .. 0x7FF  copy of aa64_smp_trampoline
 *   0x800 .. 0xBFF  AA64SMPBootParam[AA64_SMP_MAX_CPUS], 128 byte stride
 *
 * The last two trampoline instructions run with the MMU already on,
 * so the boot page needs an executable identity mapping. The firmware
 * map in TTBR0 is no good for that: edk2 maps free RAM execute-never,
 * and it goes away in AuVmmngrBootFree anyway. So the AP gets a private
 * two level map (one 1GB block around the boot page) for TTBR0, with
 * the TTBR0 half of TCR forced to 4K granule / 48 bit to match it.
 */
#define AA64_SMP_PARAM_OFFSET 0x800
#define AA64_SMP_PARAM_STRIDE 128

/* how long the boot core waits for one AP to report in */
#define AA64_SMP_ONLINE_TIMEOUT_MS 500

/* TCR_EL1 bits [15:0] describe TTBR0 walks only: T0SZ=16, IRGN0/ORGN0
 * write back, SH0 inner shareable, TG0 4K */
#define AA64_SMP_TCR_TTBR0_MASK	 0xFFFFULL
#define AA64_SMP_TCR_TTBR0_IDMAP ((16ULL << 0) | (1ULL << 8) | (1ULL << 10) | (3ULL << 12))

#define AA64_SMP_BLOCK_1G (1ULL << 30)

extern uint8_t aa64_smp_trampoline[];
extern uint8_t aa64_smp_trampoline_end[];
extern char vectors[];
extern int64_t aa64_psci_call_hvc(uint64_t fn, uint64_t arg1, uint64_t arg2, uint64_t arg3);
extern int64_t aa64_psci_call_smc(uint64_t fn, uint64_t arg1, uint64_t arg2, uint64_t arg3);

static uint64_t _smp_boot_page;
static uint64_t _smp_idmap_root;
static uint32_t _smp_cpu_count = 1;
static uint32_t _smp_next_slot = 1;
static bool _smp_psci_hvc;

_Static_assert(sizeof(AA64SMPBootParam) <= AA64_SMP_PARAM_STRIDE,
			   "AA64SMPBootParam outgrew its boot page slot");
_Static_assert(AA64_SMP_PARAM_OFFSET + AA64_SMP_MAX_CPUS * AA64_SMP_PARAM_STRIDE <= PAGE_SIZE,
			   "AA64 SMP boot page overflow");

/* Hal/aa64smpboot.s reads these by fixed offset, BaseHdr's stddef.h
 * has no offsetof so define one defensively */
#ifndef offsetof
#define offsetof(type, member) __builtin_offsetof(type, member)
#endif
_Static_assert(offsetof(AA64SMPBootParam, entry) == 0, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, stack_top) == 8, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, param_virt) == 16, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, idmap_ttbr0) == 24, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, ttbr1) == 32, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, idmap_tcr) == 40, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, mair) == 48, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, sctlr) == 56, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, vbar) == 64, "smp boot param layout");
_Static_assert(offsetof(AA64SMPBootParam, exception_stack_top) == 120, "smp boot param layout");

/**
 * @brief AA64PSCICall -- call into PSCI firmware through the conduit
 * named by the device tree
 * @param fn -- PSCI function id
 */
static int64_t AA64PSCICall(uint64_t fn, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
	if (_smp_psci_hvc)
		return aa64_psci_call_hvc(fn, arg1, arg2, arg3);
	return aa64_psci_call_smc(fn, arg1, arg2, arg3);
}

/**
 * @brief AA64SMPGetBootParam -- returns the higher half address of a
 * cpu's slot in the boot page
 * @param cpu_num -- logical cpu number
 */
static AA64SMPBootParam* AA64SMPGetBootParam(uint32_t cpu_num) {
	return (AA64SMPBootParam*)(P2V(_smp_boot_page) + AA64_SMP_PARAM_OFFSET +
							   cpu_num * AA64_SMP_PARAM_STRIDE);
}

/**
 * @brief AA64SMPAPEntry -- higher half entry of every secondary core,
 * jumped to by aa64_smp_trampoline with MMU, caches and FPU on
 * @param param -- this core's boot parameter block
 */
void AA64SMPAPEntry(AA64SMPBootParam* param) {
	/* off the trampoline's identity map, onto the boot core's tables */
	write_tcr_el1(param->tcr);
	write_ttbr0_el1((uint64_t*)param->ttbr0);
	isb_flush();
	tlb_flush_vmalle1is();

	uint32_t cpu_num = (uint32_t)param->cpu_num;
	AA64PerCPUInitialize(cpu_num, param->mpidr, param->exception_stack_top);
	AA64PerCPU* self = AA64GetPerCPU();
	self->state = AA64_CPU_STATE_ONLINE;

	/* banked GIC state (GICv2 CPU interface / GICv3 redistributor) can
	 * only be programmed from the core itself */
	GICInitializeCPU(cpu_num, param->mpidr);
	UARTDebugOut("From core : %d mpidr : %x \r\n", (uint64_t)cpu_num, param->mpidr);

	/* report in, the boot core is spinning on this */
	dmb_ish();
	*(volatile uint64_t*)&param->online = 1;
	dsb_ish();

	/* irqs stay masked until AA64SMPStartSchedulers hands us an idle
	 * thread. wfe cannot miss the wakeup: a sev between the load and the
	 * wfe leaves the event register set and wfe falls straight through */
	while (!__atomic_load_n(&self->sched_release, __ATOMIC_ACQUIRE))
		__asm__ volatile("wfe");

	AuSchedEnterAPIdle();
}

/**
 * @brief AA64SMPBuildIdentityMap -- build a tiny TTBR0 table that maps
 * the 1GB block holding the boot page 1:1, normal memory, executable
 * @param phys -- physical address to cover
 * @return physical address of the L0 table, PMM_INVALID_PHYS on failure
 */
static uint64_t AA64SMPBuildIdentityMap(uint64_t phys) {
	uint64_t l0 = AuPmmngrAllocPage(AURORA_PAGE_KERNEL);
	if (l0 == PMM_INVALID_PHYS)
		return PMM_INVALID_PHYS;
	uint64_t l1 = AuPmmngrAllocPage(AURORA_PAGE_KERNEL);
	if (l1 == PMM_INVALID_PHYS) {
		AuPmmngrReleasePage(l0);
		return PMM_INVALID_PHYS;
	}

	uint64_t* l0_table = (uint64_t*)P2V(l0);
	uint64_t* l1_table = (uint64_t*)P2V(l1);
	memset(l0_table, 0, PAGE_SIZE);
	memset(l1_table, 0, PAGE_SIZE);

	l0_table[(phys >> 39) & 0x1FF] = l1 | PTE_VALID | PTE_TABLE;
	/* AttrIndx 1 is normal write back in the kernel's MAIR */
	l1_table[(phys >> 30) & 0x1FF] = (phys & ~(AA64_SMP_BLOCK_1G - 1)) | PTE_VALID | PTE_BLOCK |
									 PTE_ATTR_IDX_1 | PTE_SH_INNER | PTE_AF | PTE_AP_RW |
									 PTE_USER_NOT_EXECUTABLE;

	aa64_dc_cvac_range(l0_table, PAGE_SIZE);
	aa64_dc_cvac_range(l1_table, PAGE_SIZE);
	return l0;
}

/**
 * @brief AA64SMPStartCPU -- fill the boot block for one core and
 * start it through PSCI CPU_ON
 * @param cpu_num -- logical cpu number to give this core
 * @param mpidr -- target core's affinity value from the device tree
 * @return true if the core reported in before the timeout
 */
static bool AA64SMPStartCPU(uint32_t cpu_num, uint64_t mpidr) {
	uint64_t stack = AuPmmngrAllocPages(AA64_SMP_AP_STACK_PAGES, 1, 0, AURORA_PAGE_KERNEL);
	if (stack == PMM_INVALID_PHYS) {
		AuTextOut("[aurora]: smp: no stack for cpu %d \r\n", (uint64_t)cpu_num);
		return false;
	}

	uint64_t exc_stack = AuPmmngrAllocPages(AA64_SMP_EXC_STACK_PAGES, 1, 0, AURORA_PAGE_KERNEL);
	if (exc_stack == PMM_INVALID_PHYS) {
		AuTextOut("[aurora]: smp: no exception stack for cpu %d \r\n", (uint64_t)cpu_num);
		AuPmmngrReleasePages(stack);
		return false;
	}

	AA64SMPBootParam* param = AA64SMPGetBootParam(cpu_num);
	memset(param, 0, sizeof(AA64SMPBootParam));
	param->entry = (uint64_t)AA64SMPAPEntry;
	param->stack_top = P2V(stack) + AA64_SMP_AP_STACK_PAGES * PAGE_SIZE;
	param->param_virt = (uint64_t)param;
	param->ttbr0 = read_ttbr0_el1();
	param->tcr = read_tcr_el1();
	param->idmap_ttbr0 = _smp_idmap_root;
	param->idmap_tcr = (param->tcr & ~AA64_SMP_TCR_TTBR0_MASK) | AA64_SMP_TCR_TTBR0_IDMAP;
	param->ttbr1 = read_ttbr1_el1();
	param->mair = read_mair_el1();
	param->sctlr = read_sctlr_el1();
	param->vbar = (uint64_t)vectors;
	param->cpu_num = cpu_num;
	param->mpidr = mpidr;
	param->online = 0;
	param->_isolate = 0;
	param->exception_stack_top = P2V(exc_stack) + AA64_SMP_EXC_STACK_PAGES * PAGE_SIZE;

	/* the trampoline reads this with the MMU off */
	aa64_dc_cvac_range(param, sizeof(AA64SMPBootParam));

	uint64_t param_phys = _smp_boot_page + AA64_SMP_PARAM_OFFSET + cpu_num * AA64_SMP_PARAM_STRIDE;
	int64_t ret = AA64PSCICall(PSCI_FN64_CPU_ON, mpidr, _smp_boot_page, param_phys);
	if (ret != PSCI_RET_SUCCESS) {
		AuTextOut("[aurora]: smp: CPU_ON mpidr %x failed (%d) \r\n", mpidr, ret);
		AuPmmngrReleasePages(stack);
		AuPmmngrReleasePages(exc_stack);
		return false;
	}

	/* plain counter poll, AA64SleepUS parks in wfi and nothing is
	 * guaranteed to wake us here */
	uint64_t timeout = (get_cntfrq_el0() / 1000) * AA64_SMP_ONLINE_TIMEOUT_MS;
	uint64_t start = get_cntpct_el0();
	while ((get_cntpct_el0() - start) < timeout) {
		if (*(volatile uint64_t*)&param->online)
			return true;
	}

	/* stack and slot stay taken, a late AP might still be running on them */
	AuTextOut(
		"[aurora]: smp: cpu %d (mpidr %x) did not come online \r\n", (uint64_t)cpu_num, mpidr);
	return false;
}

/**
 * @brief AA64SMPInitialize -- start every secondary core described
 * in the device tree through PSCI CPU_ON
 */
void AA64SMPInitialize() {
	/* boot core (logical cpu 0) per cpu data is set up earlier by
	 * AA64PerCPUBootInitialize, called from _AuMain */
	uint32_t* cpus = AuDeviceTreeGetNode("cpus");
	if (!cpus) {
		AuTextOut("[aurora]: smp: no /cpus node, staying uniprocessor \r\n");
		AuDeviceTreeDumpAll();
		return;
	}

	uint32_t* psci = AuDeviceTreeGetNode("psci");
	const char* method = AuDeviceTreeGetStringProperty(psci, "method");
	if (!method) {
		AuTextOut("[aurora]: smp: no psci method, staying uniprocessor \r\n");
		return;
	}
	_smp_psci_hvc = (strcmp(method, "hvc") == 0);

	int64_t version = AA64PSCICall(PSCI_FN_VERSION, 0, 0, 0);
	AuTextOut("[aurora]: smp: psci %d.%d via %s \r\n",
			  (uint64_t)((version >> 16) & 0xFFFF),
			  (uint64_t)(version & 0xFFFF),
			  method);

	size_t tramp_size = (size_t)(aa64_smp_trampoline_end - aa64_smp_trampoline);
	if (tramp_size > AA64_SMP_PARAM_OFFSET) {
		AuTextOut("[aurora]: smp: trampoline too big (%d bytes) \r\n", (uint64_t)tramp_size);
		return;
	}

	_smp_boot_page = AuPmmngrAllocPage(AURORA_PAGE_KERNEL);
	if (_smp_boot_page == PMM_INVALID_PHYS) {
		AuTextOut("[aurora]: smp: no memory for boot page \r\n");
		return;
	}

	void* boot_page = (void*)P2V(_smp_boot_page);
	memset(boot_page, 0, PAGE_SIZE);
	memcpy(boot_page, aa64_smp_trampoline, tramp_size);
	/* the AP fetches this with MMU and caches off */
	aa64_dc_cvac_range(boot_page, tramp_size);

	/* TODO: boot page and identity map are kept for good (3 pages),
	 * free them once every started core is known to be past the
	 * trampoline */
	_smp_idmap_root = AA64SMPBuildIdentityMap(_smp_boot_page);
	if (_smp_idmap_root == PMM_INVALID_PHYS) {
		AuTextOut("[aurora]: smp: no memory for identity map \r\n");
		AuPmmngrReleasePage(_smp_boot_page);
		return;
	}

	uint32_t addr_cells = AuDeviceTreeGetAddressCells(cpus);
	if (addr_cells == 0)
		addr_cells = 1;
	uint64_t boot_mpidr = read_mpidr_el1() & AA64_MPIDR_AFF_MASK;

	/** Parse device tree and verify cpu cores here */
	for (uint32_t i = 0;; i++) {
		uint32_t* cpu = AuDeviceTreeGetChildNode(cpus, i);
		if (!cpu)
			break;

		/* /cpus also holds cpu-map, l2-cache and friends */
		const char* type = AuDeviceTreeGetStringProperty(cpu, "device_type");
		if (!type || strcmp(type, "cpu") != 0)
			continue;

		uint64_t mpidr = AuDeviceTreeGetRegAddress(cpu, addr_cells) & AA64_MPIDR_AFF_MASK;
		if (mpidr == boot_mpidr)
			continue;

		if (_smp_next_slot >= AA64_SMP_MAX_CPUS) {
			AuTextOut("[aurora]: smp: more than %d cpus, ignoring the rest \r\n",
					  (uint64_t)AA64_SMP_MAX_CPUS);
			break;
		}

		const char* enable = AuDeviceTreeGetStringProperty(cpu, "enable-method");
		if (!enable || strcmp(enable, "psci") != 0) {
			/* TODO: spin-table (RPi3) */
			AuTextOut("[aurora]: smp: cpu mpidr %x enable-method %s not supported \r\n",
					  mpidr,
					  enable ? enable : "none");
			continue;
		}

		/* a slot is never reused, even if its core failed or timed out */
		uint32_t slot = _smp_next_slot++;
		if (AA64SMPStartCPU(slot, mpidr))
			_smp_cpu_count++;
	}

	AuTextOut("[aurora]: smp: %d cpu(s) online \r\n", (uint64_t)_smp_cpu_count);
}

/**
 * @brief AA64SMPStartSchedulers -- give every online secondary core its
 * idle thread and release it from its wait loop, called by
 * AuSchedulerStart on the boot core
 */
void AA64SMPStartSchedulers() {
	for (uint32_t cpu = 1; cpu < _smp_next_slot; cpu++) {
		AA64PerCPU* p = &_aa64_percpu[cpu];
		if (p->magic != AA64_PERCPU_MAGIC || p->state != AA64_CPU_STATE_ONLINE)
			continue;
		p->idleThread = AuSchedCreateAPIdleThread(cpu);
		if (!p->idleThread) {
			AuTextOut("[aurora]: smp: no idle thread for cpu %d \r\n", (uint64_t)cpu);
			continue;
		}
		/* idleThread must be visible before the release flag */
		__atomic_store_n(&p->sched_release, 1, __ATOMIC_RELEASE);
		dsb_ish();
		__asm__ volatile("sev");
	}
}

_Static_assert(sizeof(AA64CPUStats) == 64, "AA64CPUStats is user space ABI (XECpuStats)");

/**
 * @brief AA64SMPGetCPUStats -- fill one AA64CPUStats per known core
 * @param out -- destination array
 * @param max -- capacity of out
 * @return number of entries written
 */
int AA64SMPGetCPUStats(AA64CPUStats* out, int max) {
	int n = 0;
	for (uint32_t c = 0; c < AA64_SMP_MAX_CPUS && n < max; c++) {
		AA64PerCPU* p = &_aa64_percpu[c];
		if (p->magic != AA64_PERCPU_MAGIC)
			continue;
		AA64CPUStats s;
		memset(&s, 0, sizeof(s));
		s.cpu = c;
		s.online = (p->state == AA64_CPU_STATE_ONLINE) && (c == 0 || p->sched_release);
		s.mpidr = p->mpidr;
		/* written by the owning core only, aligned 64 bit loads */
		s.busy_ticks = p->busy_cnt;
		s.idle_ticks = p->idle_cnt;
		s.context_switches = p->context_switches;
		s.timer_irqs = p->tick_count;
		s.counter_freq = p->timer_freq;
		memcpy(s.current, p->cur_name, sizeof(s.current));
		s.current[sizeof(s.current) - 1] = '\0';
		memcpy(&out[n++], &s, sizeof(s));
	}
	return n;
}

/**
 * @brief AA64SMPIPILocalInit -- enable the IPI SGIs on the calling core
 */
void AA64SMPIPILocalInit() {
#ifndef __TARGET_BOARD_RPI3__
	GICEnablePPILocal(AA64_IPI_RESCHED);
	GICEnablePPILocal(AA64_IPI_DEFERRED_IRQ);
#endif
}

/**
 * @brief AA64SMPGetCPUCount -- returns the number of online cores,
 * boot core included
 */
uint32_t AA64SMPGetCPUCount() {
	return _smp_cpu_count;
}
