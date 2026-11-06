/* Exercises the real AArch64 process/SHM/VMM teardown with fake physical
 * memory and scheduler hooks. Run with Tests/test_process_teardown.sh.
 * ASan catches accesses after kfree; fresh/freed allocations are also
 * poisoned like DEBUG_ALLOC. Check exact frame and heap restoration. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <process.h>
#include <clean.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/shm.h>
#include <Mm/tlsf.h>
#include <Mm/vmmngr.h>
#include <Hal/AA64/aa64lowlevel.h>
#include <Ipc/postbox.h>
#include <Sound/sound.h>
#include <aucon.h>
#include <timer.h>
#include <ftmngr.h>

extern FontSeg* FontManagerAllocateSegment(AuVFSNode* fontfile, char* fontname);
extern void FontManagerRemoveSegment(FontSeg* seg);
extern uint16_t fontKey;

#define FRAME_COUNT 2048
static unsigned char physical[FRAME_COUNT * PAGE_SIZE] __attribute__((aligned(4096)));
static struct {
	unsigned refs;
	int32_t owner;
	int64_t backing;
} frames[FRAME_COUNT];
static size_t live_frames, live_allocs;
static uint64_t ttbr0, ttbr1;
static AA64Thread reaper, *current = &reaper;
static unsigned unblocked, checks, tlbis;
static int expected_pid;
static tlsf_pool_t heap;
extern uint64_t* _RootPagingKe;

/* The header must preserve alignment for AA64Thread's SIMD registers. */
typedef union {
	size_t size;
	max_align_t alignment;
} HostAllocation;

typedef struct {
	size_t frames;
	size_t allocations;
	size_t bytes;
} MemoryUsage;

static MemoryUsage memory_usage(void) {
	MemoryUsage usage = {live_frames, live_allocs, heap.used_size};
	return usage;
}

static void assert_memory_usage(MemoryUsage expected) {
	assert(live_frames == expected.frames);
	assert(live_allocs == expected.allocations);
	assert(heap.used_size == expected.bytes);
}

void* kmalloc(unsigned int size) {
	HostAllocation* allocation = malloc(sizeof(*allocation) + size);
	assert(allocation);
	allocation->size = size;
	void* p = allocation + 1;
	memset(p, 0xAA, size);
	++live_allocs;
	heap.used_size += size;
	return p;
}

void kfree(void* p) {
	if (!p)
		return;
	HostAllocation* allocation = (HostAllocation*)p - 1;
	heap.used_size -= allocation->size;
	memset(p, 0x6B, allocation->size);
	--live_allocs;
	free(allocation);
}

uint64_t P2V(uint64_t phys) {
	assert(phys < sizeof(physical));
	return (uint64_t)(physical + phys);
}

uint64_t V2P(uint64_t virt) {
	assert(virt >= (uint64_t)physical && virt < (uint64_t)(physical + sizeof(physical)));
	return virt - (uint64_t)physical;
}

uint64_t AuPmmngrAllocPageForOwner(uint8_t type, int32_t owner) {
	(void)type;
	for (size_t i = 1; i < FRAME_COUNT; ++i) {
		if (frames[i].refs)
			continue;
		frames[i].refs = 1;
		frames[i].owner = owner;
		frames[i].backing = -1;
		memset(physical + i * PAGE_SIZE, 0, PAGE_SIZE);
		++live_frames;
		return i * PAGE_SIZE;
	}
	abort();
}

uint64_t AuPmmngrAllocPage(uint8_t type) {
	return AuPmmngrAllocPageForOwner(type, PMM_OWNER_KERNEL);
}

bool AuPmmngrReleasePage(uint64_t phys) {
	assert(phys && phys % PAGE_SIZE == 0 && phys < sizeof(physical));
	size_t i = phys / PAGE_SIZE;
	assert(frames[i].refs); /* A double release must fail the regression. */
	if (!--frames[i].refs) {
		--live_frames;
		memset(physical + phys, 0x6B, PAGE_SIZE);
	}
	return true;
}

bool AuPmmngrRetainPage(uint64_t phys) {
	assert(frames[phys / PAGE_SIZE].refs);
	++frames[phys / PAGE_SIZE].refs;
	return true;
}

int64_t AuPmmngrGetBackingBlock(uint64_t phys) {
	assert(phys < sizeof(physical));
	return frames[phys / PAGE_SIZE].backing;
}

uint64_t AuPmmOwnerPages(int32_t owner) {
	uint64_t count = 0;
	for (size_t i = 1; i < FRAME_COUNT; ++i)
		if (frames[i].refs && frames[i].owner == owner)
			++count;
	return count;
}

void AuPmmOwnerTeardownCheck(int32_t owner, const char* tag) {
	assert(!expected_pid || owner == expected_pid);
	assert(AuProcessFindPID(owner));
	assert(strcmp(tag, "lifecycle") == 0); /* Must be read before kfree(proc). */
	assert(AuPmmOwnerPages(owner) == 0);
	++checks;
}

void AuPmmngrGetStats(AuPmmStats* stats) {
	memset(stats, 0, sizeof(*stats));
	stats->managed_pages = FRAME_COUNT;
	stats->allocated_pages = live_frames;
	stats->free_pages = FRAME_COUNT - live_frames;
}

tlsf_pool_t* tlsf_get_pool(void) {
	return &heap;
}
uint64_t read_ttbr0_el1(void) {
	return ttbr0;
}
uint64_t read_ttbr1_el1(void) {
	return ttbr1;
}
void dsb_ish(void) {}
void dmb_ish(void) {}
void dmb_sy(void) {}
void dsb_sy_barrier(void) {}
void isb_flush(void) {}
void tlb_flush(uint64_t va) {
	(void)va;
	++tlbis;
}
void tlb_flush_vmalle1is(void) {
	++tlbis;
}
void aa64_data_cache_clean_range(void* p, size_t n) {
	(void)p;
	(void)n;
}
void data_cache_flush(uint64_t* p) {
	(void)p;
}
void UARTDebugOut(const char* format, ...) {
	(void)format;
}
void AuTextOut(const char* format, ...) {
	(void)format;
}
AA64Thread* AuGetCurrentThread(void) {
	return current;
}
void AuThreadMoveToTrash(AA64Thread* t) {
	t->state = THREAD_STATE_KILLABLE;
}
void AuThreadCleanTrash(AA64Thread* t) {
	assert(t != current);
}
void AuUnblockThread(AA64Thread* t) {
	assert(t);
	++unblocked;
	t->state = THREAD_STATE_READY;
}
void AuBlockThread(AA64Thread* t) {
	t->state = THREAD_STATE_BLOCKED;
}
/* This fixture creates neither IPC endpoints nor per-thread wait queues.
 * Keep those newer teardown hooks explicit rather than requiring a scheduler
 * or IPC server in this focused memory-lifecycle test. */
void AuIpcProcessGone(AuProcess* proc) {
	assert(proc);
}
void AuThreadAwakeWaiters(AA64Thread* thread) {
	assert(thread);
	assert(!thread->waitlist || thread->waitlist->pointer == 0);
}
void AuSoundRemoveDSP(uint16_t id) {
	(void)id;
}
void PostBoxDestroyByID(uint16_t id) {
	(void)id;
}
int AuGetTimerByThread(void* t) {
	(void)t;
	return -1;
}
void AuroraTimerCancel(int id) {
	(void)id;
}
void BordoisilaCapCleanupProcess(void* proc) {
	(void)proc;
}
void AuDevFSNodeClose(AuVFSNode* node) {
	(void)node;
}
uint64_t AuGetCurrentUS(void) {
	return 0;
}
Spinlock* AuCreateSpinlock(bool early) {
	(void)early;
	return kmalloc(sizeof(Spinlock));
}
void AuDeleteSpinlock(Spinlock* lock) {
	kfree(lock);
}

static uint64_t map_private(AuProcess* proc, uint64_t va) {
	uint64_t phys = AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
	assert(AuMapPageEx(proc->cr3, phys, va, PTE_NORMAL_MEM | PTE_AP_RW_USER));
	return phys;
}

static uint64_t* leaf_at(uint64_t* root, uint64_t va) {
	for (int shift = 39; shift > 12; shift -= 9)
		root = (uint64_t*)P2V(root[(va >> shift) & 511] & 0x0000FFFFFFFFF000ULL);
	return &root[(va >> 12) & 511];
}

static AA64Thread* make_thread(AuProcess* proc, uint64_t stack) {
	AA64Thread* t = kmalloc(sizeof(*t));
	memset(t, 0, sizeof(*t));
	t->state = THREAD_STATE_READY;
	t->uentry = kmalloc(sizeof(*t->uentry));
	memset(t->uentry, 0, sizeof(*t->uentry));
	t->uentry->stackBase = stack;
	t->procSlot = proc;
	/* Reuse the same kernel VA each cycle: it must be unmapped on exit. */
	uint64_t kstack =
		KERNEL_STACK_LOCATION + (stack == proc->_main_stack_ ? 0 : KERNEL_STACK_SIZE * 2);
	for (size_t i = 0; i < KERNEL_STACK_SIZE / PAGE_SIZE; ++i)
		assert(AuMapPage(AuPmmngrAllocPage(AURORA_PAGE_NORMAL),
						 kstack + i * PAGE_SIZE,
						 PTE_NORMAL_MEM));
	t->originalKSp = kstack + KERNEL_STACK_SIZE - 64;
	return t;
}

static void test_cycle(uint64_t cached, uint64_t device, MemoryUsage baseline) {
	AuProcess* proc = AuCreateProcessSlot(NULL, "lifecycle");
	expected_pid = proc->proc_id;
	proc->main_thread = make_thread(proc, proc->_main_stack_);
	AA64Thread* sub = make_thread(proc, (uint64_t)CreateSubUserStack(proc, proc->cr3));
	proc->threads[0] = sub;
	proc->threads[1] = proc->main_thread; /* Alias must not be freed twice. */
	proc->num_thread = 2;
	map_private(proc, 0x600000000); /* Kernel-loaded executable. */
	assert(AuMapPageEx(proc->cr3, AuPmmngrAllocPage(AURORA_PAGE_NORMAL),
		0xD0000000, PTE_NORMAL_MEM | PTE_AP_RW_USER)); /* Kernel-owned signal trampoline. */
	map_private(proc, PROCESS_BREAK_ADDRESS);
	uint64_t sparse_va = PROCESS_MMAP_ADDRESS + 0x800000;
	map_private(proc, sparse_va);
	*leaf_at(proc->cr3, sparse_va) |= PTE_USER_NOT_EXECUTABLE | PTE_KERNEL_NOT_EXECUTABLE;
	map_private(proc, 0x40000000000); /* argv page */
	assert(AuMapPageEx(proc->cr3, cached, sparse_va + PAGE_SIZE, PTE_NORMAL_MEM | PTE_AP_RW_USER));
	assert(AuMapPageEx(proc->cr3, device, sparse_va + 2 * PAGE_SIZE, PTE_DEVICE_MEM | PTE_AP_RW_USER));
	/* No length counters populated: actual mappings determine reclamation. */
	for (int i = 0; i < 3; ++i)
		list_add(proc->vmareas, kmalloc(32));
	AA64Thread waiters[3] = {0};
	for (int i = 0; i < 3; ++i)
		list_add(proc->waitlist, &waiters[i]);
	/* Kernel arguments still owned if the child never got its first run. */
	AuUserEntry* u = proc->main_thread->uentry;
	u->num_args = 2;
	u->argvs = kmalloc(2 * sizeof(char*));
	u->argvs[0] = kmalloc(8);
	u->argvs[1] = kmalloc(8);
	AuVFSNode* file = kmalloc(sizeof(*file));
	memset(file, 0, sizeof(*file));
	file->flags = FS_FLAG_GENERAL;
	strcpy(file->filename, "test");
	proc->fds[3] = file;
	proc->fds[4] = file;

	current = sub; /* An abort in a sub-thread must also stop the main thread. */
	ttbr0 = V2P((uint64_t)proc->cr3);
	AuProcessExit(proc, false);
	assert(unblocked == 3);
	unblocked = 0;
	assert(proc->main_thread->state == THREAD_STATE_KILLABLE);
	assert(AuGetKillableProcess() == NULL); /* Still executing on dead stack. */
	MemoryUsage during = memory_usage();
	AuProcessClean(NULL, proc);
	assert_memory_usage(during);
	current = &reaper;
	ttbr0 = ttbr1;
	assert(AuGetKillableProcess() == proc);
	AuProcessClean(NULL, proc);
	assert_memory_usage(baseline);
	assert(AuProcGetNumProcessCount() == 0);
	assert(frames[cached / PAGE_SIZE].refs == 1);
	assert(frames[device / PAGE_SIZE].refs == 1);
	assert(AuGetPhysicalAddress(KERNEL_STACK_LOCATION) == NULL);
}

static void test_unmap_holes(void) {
	uint64_t* root = AuCreateVirtualAddressSpace();
	uint64_t va = 0x1FFFF000; /* First page missing, next crosses an L2 boundary. */
	uint64_t phys = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	assert(AuMapPageEx(root, phys, va + PAGE_SIZE, PTE_NORMAL_MEM | PTE_AP_RW_USER));
	unsigned before_tlbi = tlbis;
	AuFreePagesEx(root, va, true, 2 * PAGE_SIZE);
	assert(!frames[phys / PAGE_SIZE].refs);
	assert(tlbis > before_tlbi);
	ttbr0 = V2P((uint64_t)root);
	size_t before_frames = live_frames;
	assert(AuVmmngrGetPage(0x7000000000, 0, VIRT_GETPAGE_ONLY_RET) == NULL);
	assert(live_frames == before_frames);
	AuVPage* created = AuVmmngrGetPage(0x7000000000, PTE_NORMAL_MEM | PTE_AP_RW_USER,
									 VIRT_GETPAGE_CREATE);
	assert(created && created->bits.present);
	assert(AuVmmngrGetPage(0x7000000000, 0, VIRT_GETPAGE_ONLY_RET) == created);
	assert(!AuDestroyVirtualAddressSpace(root)); /* Never destroy active root. */
	ttbr0 = ttbr1;
	assert(AuDestroyVirtualAddressSpace(root));
}

static void test_shm_gap(void) {
	AuProcess* proc = AuCreateProcessSlot(NULL, "lifecycle");
	expected_pid = proc->proc_id;
	ttbr0 = V2P((uint64_t)proc->cr3);
	unsigned before_tlbi = tlbis;
	for (int i = 0; i < 2; ++i) {
		int id = AuCreateSHM(proc, 123, PAGE_SIZE, 0);
		assert(AuSHMObtainMem(proc, id, NULL, 0));
		AuSHMUnmap(123, proc); /* Second obtain takes the empty-list gap path. */
		assert(AuGetSHMByID(id) == NULL);
	}
	/* Reuse a gap between live mappings, then grow past an undersized gap. */
	int first = AuCreateSHM(proc, 124, PAGE_SIZE, 0);
	int middle = AuCreateSHM(proc, 125, 2 * PAGE_SIZE, 0);
	int last = AuCreateSHM(proc, 126, PAGE_SIZE, 0);
	uint64_t first_va = (uint64_t)AuSHMObtainMem(proc, first, NULL, 0);
	uint64_t middle_va = (uint64_t)AuSHMObtainMem(proc, middle, NULL, 0);
	uint64_t last_va = (uint64_t)AuSHMObtainMem(proc, last, NULL, 0);
	assert(middle_va == first_va + PAGE_SIZE);
	assert(last_va == middle_va + 2 * PAGE_SIZE);
	AuSHMUnmap(125, proc);
	int replacement = AuCreateSHM(proc, 127, 2 * PAGE_SIZE, 0);
	assert((uint64_t)AuSHMObtainMem(proc, replacement, NULL, 0) == middle_va);
	AuSHMUnmap(127, proc);
	int larger = AuCreateSHM(proc, 128, 3 * PAGE_SIZE, 0);
	assert((uint64_t)AuSHMObtainMem(proc, larger, NULL, 0) == last_va + PAGE_SIZE);
	assert(tlbis > before_tlbi);
	ttbr0 = ttbr1;
	AuProcessExit(proc, false); /* No main thread: partially created slot. */
	AuProcessClean(NULL, proc);
}

static void test_reap_all(void) {
	AuProcess reaper_proc = {0};
	AuProcess* procs[2];
	for (int i = 0; i < 2; ++i) {
		procs[i] = AuCreateProcessSlot(NULL, "lifecycle");
		AuProcessExit(procs[i], false);
	}
	expected_pid = 0; /* Each check still requires a live process with zero pages. */
	unsigned before_checks = checks;
	assert(AuProcessWaitForTermination(&reaper_proc, -1) == -1);
	assert(checks == before_checks + 2);
	assert(AuProcGetNumProcessCount() == 0);
}

static void test_shm_peers(void) {
	AuProcess* a = AuCreateProcessSlot(NULL, "lifecycle");
	AuProcess* b = AuCreateProcessSlot(NULL, "lifecycle");
	int id = AuCreateSHM(a, 321, PAGE_SIZE, 0);
	ttbr0 = V2P((uint64_t)a->cr3);
	uint64_t va = (uint64_t)AuSHMObtainMem(a, id, NULL, 0);
	uint64_t phys = *leaf_at(a->cr3, va) & 0x0000FFFFFFFFF000ULL;
	ttbr0 = V2P((uint64_t)b->cr3);
	assert(AuSHMObtainMem(b, id, NULL, 0));
	uint64_t peer_leaf = *leaf_at(b->cr3, va);
	/* Unmapping from a different TTBR0 must clear only A's PTE. */
	AuSHMUnmap(321, a);
	assert(!*leaf_at(a->cr3, va));
	assert(*leaf_at(b->cr3, va) == peer_leaf);
	assert(frames[phys / PAGE_SIZE].refs == 1);
	assert(AuGetSHMByID(id)->link_count == 1);
	AuSHMUnmap(321, a); /* No matching mapping must not delete B's segment. */
	assert(AuGetSHMByID(id)->link_count == 1);
	AuSHMUnmapAll(a);
	ttbr0 = ttbr1;
	AuProcessExit(a, false);
	AuProcessExit(b, false);
	assert(!frames[phys / PAGE_SIZE].refs);
	expected_pid = 0;
	AuProcessClean(NULL, a);
	AuProcessClean(NULL, b);
}

static void test_font_cache(void) {
	AuVFSNode file = {0};
	file.size = PAGE_SIZE + 1;
	fontKey = 0x1234;
	FontSeg* font = FontManagerAllocateSegment(&file, "Consolas");
	assert(font && font->sharedSeg);
	AuSHM* segment = font->sharedSeg;
	uint16_t id = segment->id;
	int font_id = AuFTMngrGetFontID("Consolas");
	assert(font_id && segment->link_count == 1);
	uint64_t phys = segment->frames[0];
	memset((void*)P2V(phys), 0x42, PAGE_SIZE);
	MemoryUsage cache_usage = memory_usage();
	for (int i = 0; i < 3; i++) {
		AuProcess* proc = AuCreateProcessSlot(NULL, "lifecycle");
		expected_pid = proc->proc_id;
		ttbr0 = V2P((uint64_t)proc->cr3);
		assert(AuSHMObtainMem(proc, id, NULL, 0));
		assert(segment->link_count == 2);
		ttbr0 = ttbr1;
		AuProcessExit(proc, false);
		AuProcessClean(NULL, proc);
		assert_memory_usage(cache_usage);
		assert(AuFTMngrGetFontID("Consolas") == font_id);
		assert(AuGetSHMByID(id) == segment && segment->link_count == 1);
		assert(frames[phys / PAGE_SIZE].refs == 1);
		assert(*(unsigned char*)P2V(phys) == 0x42);
	}
	FontManagerRemoveSegment(font);
	assert(AuGetSHMByID(id) == NULL);
	assert(!frames[phys / PAGE_SIZE].refs);
	kfree(font);
}

int main(void) {
	ttbr1 = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	ttbr0 = ttbr1;
	_RootPagingKe = (uint64_t*)ttbr1;
	uint64_t cached = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	uint64_t device = AuPmmngrAllocPage(AURORA_PAGE_NORMAL);
	frames[cached / PAGE_SIZE].backing = 42;
	/* Warm shared kernel-stack page tables before measuring a baseline. */
	assert(AuMapPage(device, KERNEL_STACK_LOCATION, PTE_DEVICE_MEM));
	AuFreePages(KERNEL_STACK_LOCATION, false, PAGE_SIZE);
	AuInitialiseSHMMan();
	MemoryUsage baseline = memory_usage();
	for (int i = 0; i < 20; ++i)
		test_cycle(cached, device, baseline);
	test_unmap_holes();
	test_shm_gap();
	test_reap_all();
	test_shm_peers();
	test_font_cache();
	assert_memory_usage(baseline);
	assert(checks == 28);
	puts("process teardown: PASS (20 poisoned lifecycles, exact heap/frame restoration, "
		 "SHM reuse, sparse mappings, font cache lifetime)");
	return 0;
}
