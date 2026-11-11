/**
 * Static AArch64 ELF loader for PROCESS_TYPE_LINUX guests.
 * Header, program headers, and PT_LOAD bytes come from the file page cache.
 * No dynamic linker and no contiguous copy of the file.
 */

#include <loader.h>
#include <Hal/AA64/sched.h>
#include <Mm/kmalloc.h>
#include <Mm/mmfile.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <Drivers/uart.h>
#include <string.h>
#include <_null.h>
#include <Hal/AA64/aa64lowlevel.h>

#define ELF_MAG0 0x7f
#define ELFCLASS64 2
#define ELFDATA2LSB 1
#define ET_EXEC 2
#define ET_DYN 3
#define EM_AARCH64 183
#define PT_LOAD 1
#define PT_INTERP 3
#define ELF_PAGE 0x400000

typedef struct _elf64_ehdr_ {
	uint8_t e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct _elf64_phdr_ {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
} Elf64_Phdr;

/* Page list is appended in pageIndex order. hint walks forward with the file. */
static AuMMPageCache* elf_find_page(AuMMFileBack* fb, AuMMPageCache** hint, size_t page_index) {
	AuMMPageCache* cache = (hint && *hint) ? *hint : fb->pageCache;
	if (cache && cache->pageIndex > page_index)
		cache = fb->pageCache;
	for (; cache; cache = cache->next) {
		if (cache->pageIndex == page_index) {
			if (hint)
				*hint = cache;
			return cache;
		}
		if (cache->pageIndex > page_index)
			break;
	}
	for (cache = fb->pageCache; cache; cache = cache->next) {
		if (cache->pageIndex == page_index) {
			if (hint)
				*hint = cache;
			return cache;
		}
	}
	return NULL;
}

/* Copy [off, off+len) from the page cache. Missing pages are a failed load. */
static int elf_cache_copy(AuMMFileBack* fb, size_t image_len, AuMMPageCache** hint, uint64_t off,
						  void* dst, size_t len) {
	uint8_t* out = (uint8_t*)dst;
	if (!fb || !out)
		return -1;
	if (off > image_len || len > image_len - off)
		return -1;
	while (len) {
		size_t page_index = (size_t)(off / PAGE_SIZE);
		size_t page_off = (size_t)(off % PAGE_SIZE);
		size_t chunk = PAGE_SIZE - page_off;
		if (chunk > len)
			chunk = len;
		AuMMPageCache* cache = elf_find_page(fb, hint, page_index);
		if (!cache) {
			UARTDebugOut("[elf]: short page cache at %x\r\n", off);
			return -1;
		}
		memcpy(out, (uint8_t*)P2V(cache->physicalPage) + page_off, chunk);
		out += chunk;
		off += chunk;
		len -= chunk;
	}
	return 0;
}

/* Physical frame already installed at virt in this process, or 0. */
static uint64_t elf_existing_phys(uint64_t* root, uint64_t virt) {
	if (!root)
		return 0;
	const long i4 = (virt >> 39) & 0x1FF;
	const long i3 = (virt >> 30) & 0x1FF;
	const long i2 = (virt >> 21) & 0x1FF;
	const long i1 = (virt >> 12) & 0x1FF;
	if (!(root[i4] & 1))
		return 0;
	uint64_t* l3 = (uint64_t*)P2V(root[i4] & ~0xFFFULL);
	if (!(l3[i3] & 1))
		return 0;
	uint64_t* l2 = (uint64_t*)P2V(l3[i3] & ~0xFFFULL);
	if (!(l2[i2] & 1))
		return 0;
	uint64_t* l1 = (uint64_t*)P2V(l2[i2] & ~0xFFFULL);
	if (!(l1[i1] & 1))
		return 0;
	return l1[i1] & ~0xFFFULL;
}

static int elf_map_segment(AuProcess* proc, AuMMFileBack* fb, size_t image_len, AuMMPageCache** hint,
						   Elf64_Phdr* ph, uint64_t bias) {
	if (ph->p_offset > image_len || ph->p_filesz > image_len - ph->p_offset)
		return -1;
	if (ph->p_memsz < ph->p_filesz)
		return -1;

	uint64_t va = ph->p_vaddr + bias;
	uint64_t page_off = va & (PAGE_SIZE - 1);
	uint64_t map_va = va - page_off;
	uint64_t map_len = PAGE_ALIGN(ph->p_memsz + page_off);
	uint64_t file_off = ph->p_offset - page_off;

	for (uint64_t done = 0; done < map_len; done += PAGE_SIZE) {
		uint64_t page_va = map_va + done;
		/* .text, .rodata and .bss of a rust image share one page. A second
		 * map used to throw that frame away, so the strings were zeros. */
		uint64_t phys = elf_existing_phys(proc->cr3, page_va);
		int fresh = 0;
		if (!phys) {
			phys = (uint64_t)AuPmmngrAllocPageForOwner(AURORA_PAGE_NORMAL, proc->proc_id);
			if (!phys)
				return -1;
			memset((void*)P2V(phys), 0, PAGE_SIZE);
			fresh = 1;
		}
		uint64_t src = file_off + done;
		if (src < ph->p_offset + ph->p_filesz && src + PAGE_SIZE > ph->p_offset) {
			uint64_t copy_from = src < ph->p_offset ? ph->p_offset : src;
			uint64_t copy_to = src + PAGE_SIZE;
			uint64_t file_end = ph->p_offset + ph->p_filesz;
			if (copy_to > file_end)
				copy_to = file_end;
			if (copy_to > copy_from) {
				if (elf_cache_copy(fb,
								   image_len,
								   hint,
								   copy_from,
								   (uint8_t*)P2V(phys) + (copy_from - src),
								   (size_t)(copy_to - copy_from)) != 0) {
					if (fresh)
						AuPmmngrReleasePage(phys);
					return -1;
				}
				/* written through the direct map, fetched at the user VA */
				AA64DCacheCleanPoU((uint8_t*)P2V(phys) + (copy_from - src),
								   (size_t)(copy_to - copy_from));
			}
		}
		if (fresh &&
			!AuMapPageEx(proc->cr3, phys, page_va, PTE_NORMAL_MEM | PTE_AP_RW_USER)) {
			UARTDebugOut("[elf]: page %x already mapped\r\n", page_va);
			AuPmmngrReleasePage(phys);
			return -1;
		}
	}
	/* frames may have held another process's code: drop stale lines */
	AA64ICacheInvalidateAll();
	return 0;
}

uint64_t AuLinuxBuildUserStack(AuUserEntry* uentry) {
	uint64_t sp = uentry->rsp & ~(uint64_t)15;
	int argc = uentry->num_args;
	if (argc < 0)
		argc = 0;
	if (argc > 32)
		argc = 32;

	uint64_t argv_user[32];
	memset(argv_user, 0, sizeof(argv_user));
	for (int i = argc - 1; i >= 0; i--) {
		char* str = uentry->argvs ? uentry->argvs[i] : "";
		if (!str)
			str = "";
		size_t len = strlen(str) + 1;
		sp -= len;
		memcpy((void*)sp, str, len);
		argv_user[i] = sp;
	}

	static const char execfn[] = "/guest.elf";
	sp -= sizeof(execfn);
	memcpy((void*)sp, execfn, sizeof(execfn));
	uint64_t execfn_user = sp;

	sp -= 16;
	uint64_t random_user = sp;
	uint64_t* rnd = (uint64_t*)sp;
	rnd[0] = 0xA5A5A5A5A5A5A5A5ULL ^ uentry->entrypoint;
	rnd[1] = 0x1234567890ABCDEFULL ^ (uint64_t)argc;

	sp &= ~(uint64_t)15;

	/* argc, argv, empty envp, auxv. AT_HWCAP advertises FP and ASIMD only. */
	uint64_t words[48];
	int n = 0;
	words[n++] = (uint64_t)argc;
	for (int i = 0; i < argc; i++)
		words[n++] = argv_user[i];
	words[n++] = 0;
	words[n++] = 0;
	words[n++] = 3;
	words[n++] = uentry->linux_phdr;
	words[n++] = 4;
	words[n++] = uentry->linux_phent;
	words[n++] = 5;
	words[n++] = uentry->linux_phnum;
	words[n++] = 6;
	words[n++] = PAGE_SIZE;
	words[n++] = 9;
	words[n++] = uentry->entrypoint;
	words[n++] = 11;
	words[n++] = 0;
	words[n++] = 12;
	words[n++] = 0;
	words[n++] = 13;
	words[n++] = 0;
	words[n++] = 14;
	words[n++] = 0;
	words[n++] = 16;
	words[n++] = 3;
	words[n++] = 17;
	words[n++] = 100;
	words[n++] = 25;
	words[n++] = random_user;
	words[n++] = 31;
	words[n++] = execfn_user;
	words[n++] = 0;
	words[n++] = 0;

	sp -= (uint64_t)n * sizeof(uint64_t);
	sp &= ~(uint64_t)15;
	memcpy((void*)sp, words, (size_t)n * sizeof(uint64_t));
	uentry->rsp = sp;
	return sp;
}

int AuLoadElfImage(AuProcess* proc, AuMMFileBack* fb, size_t image_len, int argc, char** argv) {
	AuMMPageCache* hint = NULL;
	Elf64_Ehdr eh;
	if (!proc || !fb || image_len < sizeof(Elf64_Ehdr))
		return -1;
	if (elf_cache_copy(fb, image_len, &hint, 0, &eh, sizeof(eh)) != 0)
		return -1;

	if (eh.e_ident[0] != ELF_MAG0 || eh.e_ident[1] != 'E' || eh.e_ident[2] != 'L' ||
		eh.e_ident[3] != 'F' || eh.e_ident[4] != ELFCLASS64 || eh.e_ident[5] != ELFDATA2LSB)
		return -1;
	if (eh.e_machine != EM_AARCH64 || (eh.e_type != ET_EXEC && eh.e_type != ET_DYN))
		return -1;
	if (eh.e_phentsize != sizeof(Elf64_Phdr) || eh.e_phnum == 0)
		return -1;
	if (eh.e_phoff > image_len || (size_t)eh.e_phnum * sizeof(Elf64_Phdr) > image_len - eh.e_phoff)
		return -1;

	size_t ph_bytes = (size_t)eh.e_phnum * sizeof(Elf64_Phdr);
	Elf64_Phdr* ph = (Elf64_Phdr*)kmalloc(ph_bytes);
	if (!ph)
		return -1;
	if (elf_cache_copy(fb, image_len, &hint, eh.e_phoff, ph, ph_bytes) != 0) {
		kfree(ph);
		return -1;
	}

	for (uint16_t i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type == PT_INTERP) {
			UARTDebugOut("[elf]: dynamic linker is not supported\r\n");
			kfree(ph);
			return -1;
		}
	}

	uint64_t min_va = ~0ULL;
	for (uint16_t i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type == PT_LOAD && ph[i].p_vaddr < min_va)
			min_va = ph[i].p_vaddr;
	}
	if (min_va == ~0ULL) {
		kfree(ph);
		return -1;
	}

	uint64_t bias = 0;
	if (eh.e_type == ET_DYN && min_va < 0x10000)
		bias = ELF_PAGE;

	for (uint16_t i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if (elf_map_segment(proc, fb, image_len, &hint, &ph[i], bias) != 0) {
			kfree(ph);
			return -1;
		}
	}
	/* AT_PHDR is the virtual address of the phdr table, not the file offset.
	 * musl walks that pointer at startup. File offset 0x40 faulted as VA 0x40. */
	uint64_t phdr_va = 0;
	int saw_phdr = 0;
	for (uint16_t i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type == 6) { /* PT_PHDR */
			phdr_va = bias + ph[i].p_vaddr;
			saw_phdr = 1;
			break;
		}
	}
	if (!saw_phdr) {
		phdr_va = bias + eh.e_phoff;
		for (uint16_t i = 0; i < eh.e_phnum; i++) {
			if (ph[i].p_type != PT_LOAD)
				continue;
			if (eh.e_phoff < ph[i].p_offset)
				continue;
			if (eh.e_phoff >= ph[i].p_offset + ph[i].p_filesz)
				continue;
			phdr_va = bias + ph[i].p_vaddr + (eh.e_phoff - ph[i].p_offset);
			break;
		}
	}
	kfree(ph);

	uint64_t entry = eh.e_entry + bias;
	UARTDebugOut("[elf]: entry %x phdr %x\r\n", entry, phdr_va);

	AA64Thread* thr = AuCreateKthread(AuProcessEntUser, proc->cr3, proc->name);
	thr->threadType = THREAD_LEVEL_USER;
	AuUserEntry* uentry = (AuUserEntry*)kmalloc(sizeof(AuUserEntry));
	memset(uentry, 0, sizeof(AuUserEntry));
	uentry->rsp = proc->_main_stack_;
	uentry->entrypoint = entry;
	uentry->stackBase = proc->_main_stack_;
	uentry->linux_phdr = phdr_va;
	uentry->linux_phent = eh.e_phentsize;
	uentry->linux_phnum = eh.e_phnum;
	uentry->num_args = argc;

	if (argc > 0 && argv) {
		char** kargv = (char**)kmalloc((size_t)argc * sizeof(char*));
		memset(kargv, 0, (size_t)argc * sizeof(char*));
		for (int i = 0; i < argc; i++) {
			size_t len = argv[i] ? strlen(argv[i]) : 0;
			kargv[i] = (char*)kmalloc(len + 1);
			if (argv[i])
				strcpy(kargv[i], argv[i]);
			else
				kargv[i][0] = 0;
		}
		kfree(argv);
		uentry->argvs = kargv;
	}

	thr->uentry = uentry;
	thr->procSlot = proc;
	proc->main_thread = thr;
	proc->type_flags |= PROCESS_TYPE_LINUX;
	return 0;
}
