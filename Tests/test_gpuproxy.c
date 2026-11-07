/* Real devgpu.c + ABI/damage helpers, with fake AArch64 page tables, DMA,
 * process identity, and IRQ state. User VAs are intentionally not host
 * pointers: any accidental direct dereference fails this regression.
 * This tests validation failures, not recoverable CPU/bus faults or SMP. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
typedef struct timeval timeval;

#include "../KernelAA64/Fs/Dev/devgpu.c"

#define USER_VA 0x12000000ULL
#define USER0 (6 * PAGE_SIZE)
#define USER1 (9 * PAGE_SIZE)
#define DMA0 (16 * PAGE_SIZE)
#define DMA1 (20 * PAGE_SIZE)
#define USER_FLAGS (PTE_VALID | PTE_TABLE | PTE_AF | PTE_AP_RW_USER | PTE_NORMAL_MEM)
static uint8_t physical[32 * PAGE_SIZE] __attribute__((aligned(4096)));
static AuProcess owner, display, stranger, *current;
static AuConsole console;
AuConsole* aucon;
static uint8_t* source;
static uint64_t irq_state;
static unsigned cleans, dma_checks;
static bool mutate_request, revoke_acquire_output;
static uint64_t request_va;
static int hw_object;

static uint64_t* table(unsigned page) {
	return (uint64_t*)(physical + page * PAGE_SIZE);
}

static uint64_t* leaf(unsigned page) {
	return &table(4)[((USER_VA >> 12) & 511) + page];
}

/* Harness access bypasses the proxy only to construct/read fake user RAM. */
static void user_transfer(uint64_t address, void* bytes, size_t len, bool write) {
	assert(address >= USER_VA && address + len <= USER_VA + 2 * PAGE_SIZE);
	uint8_t* p = bytes;
	while (len) {
		uint64_t relative = address - USER_VA;
		size_t offset = relative & PAGE_MASK;
		size_t n = PAGE_SIZE - offset;
		if (n > len) n = len;
		uint8_t* ram = physical + (relative < PAGE_SIZE ? USER0 : USER1) + offset;
		if (write) memcpy(ram, p, n);
		else memcpy(p, ram, n);
		p += n;
		address += n;
		len -= n;
	}
}

uint64_t P2V(uint64_t phys) {
	assert(irq_state); /* lookup/copy stays inside the UP critical section */
	assert(phys < sizeof physical);
	return (uint64_t)(uintptr_t)(physical + phys);
}

uint64_t read_daif(void) { return irq_state; }
void mask_irqs(void) { irq_state = 1; }
void restore_daif(uint64_t value) { irq_state = value; }

void aa64_data_cache_clean_range(void* ptr, size_t len) {
	assert(irq_state && ptr && len);
	++cleans;
}

AuProcess* AuHwdevCurrentProcess(void) { return current; }
bool AuHwdevCallerAllowed(AuProcess* proc) { return proc == &owner; }
AuProcess* AuProcessTokenGetProc(uint8_t token) {
	assert(token == PROCESS_TOKEN_DISPLAY);
	return &display;
}

bool AuHwdevHandleIsDevice(AuProcess* proc, int fd, uint16_t vendor, uint16_t device) {
	assert(irq_state);
	if (mutate_request) {
		GpuSrvAcquire bad = {0};
		bad.width = bad.height = UINT32_MAX;
		bad.backing_count = UINT32_MAX;
		user_transfer(request_va, &bad, sizeof bad, true);
		mutate_request = false;
	}
	return proc == &owner && fd == 7 && vendor == GPUSRV_VENDOR && device == GPUSRV_DEVICE;
}

bool AuHwdevHandleOwnsDma(AuProcess* proc, int fd, uint64_t phys, uint64_t len) {
	assert(irq_state);
	++dma_checks;
	return proc == &owner && fd == 7 && (phys == DMA0 || phys == DMA1) && len <= PAGE_SIZE;
}

void* AuHwdevHandleObject(AuProcess* proc, int fd) {
	assert(irq_state && proc == &owner && fd == 7);
	if (revoke_acquire_output) {
		*leaf(1) = 0;
		revoke_acquire_output = false;
	}
	return &hw_object;
}

/* Device registration is linked but not needed by the ioctl regression. */
void* kmalloc(unsigned int size) { (void)size; abort(); }
AuVFSNode* AuVFSFind(char* path) { (void)path; return NULL; }
int AuDevFSAddFile(AuVFSNode* fs, char* path, AuVFSNode* node) {
	(void)fs; (void)path; (void)node; abort();
}
GID_NUM AuCredGetGroupID(uint8_t category) { return category; }
void UARTDebugOut(const char* format, ...) { (void)format; }

static int ioctl_as(AuProcess* proc, int code, uint64_t address) {
	current = proc;
	uint64_t saved = irq_state;
	int rc = gpu_ioctl(NULL, code, (void*)(uintptr_t)address);
	assert(irq_state == saved);
	return rc;
}

static GpuSrvAcquire request(void) {
	GpuSrvAcquire r = {0};
	r.version = GPUSRV_VERSION;
	r.hw_handle = 7;
	r.width = 4;
	r.height = 3;
	r.backing_count = 2;
	r.backing[0].phys = DMA0;
	r.backing[0].length = 20;
	r.backing[1].phys = DMA1;
	r.backing[1].length = 28;
	return r;
}

static void reset(void) {
	if (gpu_owner) AuGpuProxyOwnerGone(gpu_owner, gpu_hw_device);
	free(source);
	/* Exactly (height - 1) * pitch + width * 4: ASan guards the last byte. */
	source = malloc(64);
	assert(source);
	memset(source, 0xEE, 64);
	for (unsigned y = 0; y < 3; ++y)
		for (unsigned x = 0; x < 16; ++x)
			source[y * 24 + x] = (uint8_t)(y * 16 + x);
	memset(physical, 0, sizeof physical);
	table(1)[(USER_VA >> 39) & 511] = 2 * PAGE_SIZE | PTE_VALID | PTE_TABLE;
	table(2)[(USER_VA >> 30) & 511] = 3 * PAGE_SIZE | PTE_VALID | PTE_TABLE;
	table(3)[(USER_VA >> 21) & 511] = 4 * PAGE_SIZE | PTE_VALID | PTE_TABLE;
	*leaf(0) = USER0 | USER_FLAGS;
	*leaf(1) = USER1 | USER_FLAGS;
	memset(&owner, 0, sizeof owner);
	owner.proc_id = 17;
	owner.cr3 = table(1);
	display.cr3 = stranger.cr3 = owner.cr3;
	console = (AuConsole){.width = 4, .height = 3, .buffer = (uint32_t*)source,
		.bpp = 32, .size = 64, .pitch = 24, .desktop_width = 100, .desktop_height = 100};
	aucon = &console;
	request_va = USER_VA + PAGE_SIZE - 128; /* ACQUIRE straddles nonadjacent frames */
	cleans = dma_checks = 0;
	mutate_request = revoke_acquire_output = false;
	assert(!irq_state && !gpu_attached && !gpu_damage.busy);
}

static int acquire(GpuSrvAcquire r) {
	user_transfer(request_va, &r, sizeof r, true);
	return ioctl_as(&owner, GPUSRV_IOC_ACQUIRE, request_va);
}

static void assert_detached(void) {
	assert(!gpu_attached && !gpu_ready && !gpu_owner && !gpu_hw_device);
	assert(!gpu_backing_count && !gpu_backing_bytes && !gpu_damage.busy);
}

static void test_geometry(void) {
	reset();
	GpuSrvAcquire r = request();
	r.backing[1].length = 100; /* ample destination capacity must not authorize larger source reads */
	r.width = 5; /* under the ABI cap, but larger than the staging surface */
	assert(acquire(r) == GPUSRV_E_INVAL);
	r.width = GPUSRV_MAX_DIM;
	assert(acquire(r) == GPUSRV_E_INVAL);
	r.width = 3;
	assert(acquire(r) == GPUSRV_E_INVAL);
	r.width = 0;
	assert(acquire(r) == GPUSRV_E_INVAL);
	r = request(); r.height = 4; r.backing[1].length = 100;
	assert(acquire(r) == GPUSRV_E_INVAL);
	r = request();
	console.size = 63;
	assert(acquire(r) == GPUSRV_E_INVAL);
	console.size = 64; console.pitch = 15;
	assert(acquire(r) == GPUSRV_E_INVAL);
	console.pitch = UINT32_MAX; console.size = UINT32_MAX;
	assert(acquire(r) == GPUSRV_E_INVAL);
	console.pitch = 24; console.size = 64; console.bpp = 24;
	assert(acquire(r) == GPUSRV_E_INVAL);
	console.bpp = 32; console.height = UINT32_MAX;
	assert(acquire(r) == GPUSRV_E_INVAL);
	console.height = 3; console.buffer = (uint32_t*)(uintptr_t)(UINT64_MAX - 16);
	assert(acquire(r) == GPUSRV_E_INVAL);
	console.buffer = (uint32_t*)source; aucon = NULL;
	assert(acquire(r) == GPUSRV_E_INVAL);
	aucon = &console;
	r.backing[1].length = 27; /* tightly packed destination one byte short */
	assert(acquire(r) == GPUSRV_E_INVAL);
	r = request(); r.backing[0].phys = UINT64_MAX & ~PAGE_MASK;
	r.backing[0].length = 8192;
	assert(acquire(r) == GPUSRV_E_INVAL);
	assert_detached();
	assert(acquire(request()) == GPUSRV_OK); /* exact source and destination extents */
	GpuSrvAcquire reply;
	user_transfer(request_va, &reply, sizeof reply, false);
	assert(reply.width == 4 && reply.height == 3 && reply.pitch == 16);
	assert(reply.backing_bytes == 48 && reply.resource == GPUSRV_RESOURCE);
	assert(reply.generation == gpu_generation && reply.generation);
}

static void test_acquire_copyout_and_snapshot(void) {
	reset();
	GpuSrvAcquire r = request(), after;
	uint64_t generation = gpu_generation;
	/* Readable input, but second output page is read-only: no partial reply. */
	*leaf(1) |= 1ULL << 7;
	assert(acquire(r) == GPUSRV_E_INVAL);
	user_transfer(request_va, &after, sizeof after, false);
	assert(memcmp(&r, &after, sizeof r) == 0);
	assert_detached();
	assert(gpu_generation == generation);
	*leaf(1) = USER1 | USER_FLAGS;
	revoke_acquire_output = true; /* mapping lost after copy-in, before reply */
	assert(acquire(r) == GPUSRV_E_INVAL);
	assert_detached();
	assert(gpu_generation == generation);
	*leaf(1) = USER1 | USER_FLAGS;
	mutate_request = true; /* mutate user request inside the first hardware check */
	dma_checks = 0;
	assert(acquire(r) == GPUSRV_OK);
	assert(dma_checks == 2 && gpu_width == 4 && gpu_backing_count == 2);
	user_transfer(request_va, &after, sizeof after, false);
	assert(after.width == 4 && after.height == 3 && after.backing_count == 2);
	assert(after.hw_handle == 7 && gpu_backing[0].phys == DMA0);
	assert(gpu_backing[2].phys == 0); /* unused entries are initialized */
}

static void test_user_page_validation(void) {
	reset();
	GpuSrvAcquire r = request();
	const uint64_t bad[] = {0, 0x0000800000000000ULL, UINT64_MAX - 7,
		USER_VA + 2 * PAGE_SIZE, 0x00007FFFFFFFFFF0ULL};
	for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; ++i)
		assert(ioctl_as(&owner, GPUSRV_IOC_ACQUIRE, bad[i]) == GPUSRV_E_INVAL);
	*leaf(1) = 0;
	assert(acquire(r) == GPUSRV_E_INVAL); /* cross-page input hole */
	const uint64_t invalid[] = {
		USER_FLAGS & ~PTE_VALID, USER_FLAGS & ~PTE_TABLE,
		USER_FLAGS & ~PTE_AP_RW_USER, USER_FLAGS & ~PTE_AF,
		USER_FLAGS & ~(7ULL << 2) /* device memory */
	};
	*leaf(1) = USER1 | USER_FLAGS;
	for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
		*leaf(0) = USER0 | invalid[i];
		assert(acquire(r) == GPUSRV_E_INVAL);
	}
	*leaf(0) = USER0 | USER_FLAGS;
	uint64_t* parent = &table(3)[(USER_VA >> 21) & 511];
	uint64_t saved = *parent;
	*parent &= ~PTE_TABLE; /* block is not a pointer to a next-level table */
	assert(acquire(r) == GPUSRV_E_INVAL);
	*parent = saved | (1ULL << 61); /* APTable disables EL0 */
	assert(acquire(r) == GPUSRV_E_INVAL);
	*parent = saved | (1ULL << 62); /* APTable read-only: copyout fails */
	assert(acquire(r) == GPUSRV_E_INVAL);
	*parent = saved;
	uint64_t* root = owner.cr3;
	owner.cr3 = NULL;
	assert(acquire(r) == GPUSRV_E_INVAL);
	owner.cr3 = root;
	assert_detached();
	assert(ioctl_as(&stranger, GPUSRV_IOC_ACQUIRE, 0) == GPUSRV_E_PERM);
	assert(acquire(r) == GPUSRV_OK);
	assert(ioctl_as(&stranger, GPUSRV_IOC_TAKE, USER_VA) == GPUSRV_E_PERM);
	assert(ioctl_as(NULL, GPUSRV_IOC_STATUS, USER_VA) == GPUSRV_E_PERM);
}

static void ack(uint64_t sequence, int expected) {
	GpuSrvAck a = {sequence};
	user_transfer(USER_VA, &a, sizeof a, true);
	assert(ioctl_as(&owner, GPUSRV_IOC_ACK, USER_VA) == expected);
}

static void test_take_ack_cleanup(void) {
	reset();
	assert(acquire(request()) == GPUSRV_OK);
	memset(physical + DMA0, 0xCC, PAGE_SIZE);
	memset(physical + DMA1, 0xCC, PAGE_SIZE);
	uint64_t output = USER_VA + PAGE_SIZE - 8;
	uint8_t before[sizeof(GpuSrvFrame)], after[sizeof(GpuSrvFrame)];
	user_transfer(output, before, sizeof before, false);
	*leaf(1) |= 1ULL << 7;
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_E_INVAL);
	assert(cleans && !gpu_damage.busy && gpu_damage.dirty);
	assert(!gpu_submitted && !gpu_completed);
	uint64_t failed_sequence = gpu_damage.next;
	user_transfer(output, after, sizeof after, false);
	assert(memcmp(before, after, sizeof before) == 0);
	*leaf(1) = USER1 | USER_FLAGS;
	/* Recheck source extents on TAKE even after a successful ACQUIRE. */
	unsigned previous_cleans = cleans;
	console.size = 63;
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_E_UNAVAILABLE);
	assert(cleans == previous_cleans && !gpu_damage.busy && gpu_damage.dirty);
	console.size = 64; console.width = 3; /* valid extent, changed geometry */
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_E_UNAVAILABLE);
	console.width = 4;
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, 0) == GPUSRV_E_INVAL);
	assert(!gpu_damage.busy && gpu_damage.dirty && !gpu_submitted);
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_OK);
	GpuSrvFrame f;
	user_transfer(output, &f, sizeof f, false);
	assert(f.x == 0 && f.y == 0 && f.width == 4 && f.height == 3);
	assert(f.sequence > failed_sequence && gpu_damage.busy && gpu_submitted == 1);
	for (unsigned i = 0; i < 48; ++i)
		assert(physical[i < 20 ? DMA0 + i : DMA1 + i - 20] == i);
	assert(physical[DMA0 + 20] == 0xCC && physical[DMA1 + 28] == 0xCC);
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_E_BUSY);
	assert(ioctl_as(&owner, GPUSRV_IOC_ACK, USER_VA + 2 * PAGE_SIZE) == GPUSRV_E_INVAL);
	assert(gpu_damage.busy && !gpu_completed);
	ack(failed_sequence, GPUSRV_E_INVAL);
	assert(gpu_damage.busy);
	ack(f.sequence, GPUSRV_OK);
	ack(f.sequence, GPUSRV_E_INVAL);
	assert(!gpu_damage.busy && gpu_completed == 1);
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_E_EMPTY);
	/* A small bottom-right rectangle exercises nonzero x/y and coalescing. */
	GpuSrvDamage damage = {3, 2, 1, 1};
	uint64_t input = USER_VA + PAGE_SIZE - 4;
	user_transfer(input, &damage, sizeof damage, true);
	*leaf(1) = 0;
	assert(ioctl_as(&display, GPUSRV_IOC_DAMAGE, input) == GPUSRV_E_INVAL);
	assert(!gpu_damage.dirty);
	*leaf(1) = USER1 | USER_FLAGS | (1ULL << 7); /* read-only input is valid */
	assert(ioctl_as(&display, GPUSRV_IOC_FLUSH, input) == GPUSRV_OK);
	*leaf(1) = USER1 | USER_FLAGS;
	memset(physical + DMA1, 0xCC, 28);
	assert(ioctl_as(&owner, GPUSRV_IOC_TAKE, output) == GPUSRV_OK);
	user_transfer(output, &f, sizeof f, false);
	assert(f.x == 3 && f.y == 2 && f.width == 1 && f.height == 1);
	assert(physical[DMA1 + 23] == 0xCC && physical[DMA1 + 24] == 44);
	AuGpuProxyDamage(0, 0, 1, 1); /* pending damage survives ACK of busy frame */
	ack(f.sequence, GPUSRV_OK);
	assert(gpu_damage.dirty && !gpu_damage.busy);
	AuGpuProxyOwnerGone(&owner, &stranger);
	assert(gpu_attached);
	AuGpuProxyOwnerGone(&owner, &hw_object);
	assert_detached();
	assert(acquire(request()) == GPUSRV_OK);
	assert(gpu_damage.dirty && !gpu_damage.busy);
}

static void test_status_output(void) {
	reset();
	assert(acquire(request()) == GPUSRV_OK);
	uint64_t output = USER_VA + PAGE_SIZE - 16;
	GpuSrvStatus status;
	uint8_t before[sizeof status], after[sizeof status];
	user_transfer(output, before, sizeof before, false);
	*leaf(1) |= 1ULL << 7;
	assert(ioctl_as(&owner, GPUSRV_IOC_STATUS, output) == GPUSRV_E_INVAL);
	user_transfer(output, after, sizeof after, false);
	assert(memcmp(before, after, sizeof before) == 0);
	*leaf(1) = USER1 | USER_FLAGS;
	assert(ioctl_as(&display, GPUSRV_IOC_STATUS, output) == GPUSRV_OK);
	user_transfer(output, &status, sizeof status, false);
	assert(status.version == GPUSRV_VERSION && status.attached && status.ready);
	assert(status.width == 4 && status.height == 3 && status.pitch == 16);
	assert(!status.reserved && !status.submitted && !status.completed);
	assert(ioctl_as(&stranger, GPUSRV_IOC_STATUS, output) == GPUSRV_E_PERM);
}

int main(void) {
	test_geometry();
	test_acquire_copyout_and_snapshot();
	test_user_page_validation();
	test_take_ack_cleanup();
	test_status_output();
	AuGpuProxyOwnerGone(&owner, &hw_object);
	free(source);
	puts("gpuproxy: geometry, page validation, snapshots, copyout and TAKE/ACK cleanup passed");
	return 0;
}
