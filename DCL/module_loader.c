#include <linux/module_loader.h>
#include <Mm/kmalloc.h>
#include <Mm/pmmngr.h>
#include <Mm/vmmngr.h>
#include <string.h>
#include <Drivers/uart.h>
#include <Hal/AA64/aa64lowlevel.h>

/* ELF64 types */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t Elf64_Sxword;

#define EI_NIDENT 16

typedef struct {
	unsigned char e_ident[EI_NIDENT];
	Elf64_Half e_type;
	Elf64_Half e_machine;
	Elf64_Word e_version;
	Elf64_Addr e_entry;
	Elf64_Off e_phoff;
	Elf64_Off e_shoff;
	Elf64_Word e_flags;
	Elf64_Half e_ehsize;
	Elf64_Half e_phentsize;
	Elf64_Half e_phnum;
	Elf64_Half e_shentsize;
	Elf64_Half e_shnum;
	Elf64_Half e_shstrndx;
} Elf64_Ehdr;

typedef struct {
	Elf64_Word sh_name;
	Elf64_Word sh_type;
	Elf64_Xword sh_flags;
	Elf64_Addr sh_addr;
	Elf64_Off sh_offset;
	Elf64_Xword sh_size;
	Elf64_Word sh_link;
	Elf64_Word sh_info;
	Elf64_Xword sh_addralign;
	Elf64_Xword sh_entsize;
} Elf64_Shdr;

typedef struct {
	Elf64_Word st_name;
	unsigned char st_info;
	unsigned char st_other;
	Elf64_Half st_shndx;
	Elf64_Addr st_value;
	Elf64_Xword st_size;
} Elf64_Sym;

typedef struct {
	Elf64_Addr r_offset;
	Elf64_Xword r_info;
	int64_t r_addend;
} Elf64_Rela;

#define ELF64_R_SYM(info)  ((info) >> 32)
#define ELF64_R_TYPE(info) ((uint32_t)(info))

/* AArch64 relocation types */
#define R_AARCH64_NONE				  0
#define R_AARCH64_ABS32				  258
#define R_AARCH64_ABS16				  259
#define R_AARCH64_PREL64			  260
#define R_AARCH64_PREL32			  261
#define R_AARCH64_PREL16			  262
#define R_AARCH64_MOVW_UABS_G0		  263
#define R_AARCH64_MOVW_UABS_G0_NC	  264
#define R_AARCH64_MOVW_UABS_G1		  265
#define R_AARCH64_MOVW_UABS_G1_NC	  266
#define R_AARCH64_MOVW_UABS_G2		  267
#define R_AARCH64_MOVW_UABS_G2_NC	  268
#define R_AARCH64_MOVW_UABS_G3		  269
#define R_AARCH64_LDST_PREL_LO19	  273
#define R_AARCH64_ADR_PREL_LO21		  274
#define R_AARCH64_ADR_PREL_PG_HI21	  275
#define R_AARCH64_ADR_PREL_PG_HI21_NC 276
#define R_AARCH64_ADD_ABS_LO12_NC	  277
#define R_AARCH64_LDST8_ABS_LO12_NC	  278
#define R_AARCH64_TSTBR14			  279
#define R_AARCH64_CONDBR19			  280
#define R_AARCH64_JUMP26			  282
#define R_AARCH64_CALL26			  283
#define R_AARCH64_LDST16_ABS_LO12_NC  284
#define R_AARCH64_LDST32_ABS_LO12_NC  285
#define R_AARCH64_LDST64_ABS_LO12_NC  286
#define R_AARCH64_MOVW_PREL_G0		  287
#define R_AARCH64_MOVW_PREL_G0_NC	  288
#define R_AARCH64_MOVW_PREL_G1		  289
#define R_AARCH64_MOVW_PREL_G1_NC	  290
#define R_AARCH64_MOVW_PREL_G2		  291
#define R_AARCH64_MOVW_PREL_G2_NC	  292
#define R_AARCH64_MOVW_PREL_G3		  293
#define R_AARCH64_LDST128_ABS_LO12_NC 299
#define R_AARCH64_MOVW_SABS_G0		  300
#define R_AARCH64_MOVW_SABS_G1		  301
#define R_AARCH64_MOVW_SABS_G2		  302
#define R_AARCH64_LD_PREL_LO19		  303
#define R_AARCH64_ABS64				  257
#define R_AARCH64_GLOB_DAT			  1025
#define R_AARCH64_JUMP_SLOT			  1026
#define R_AARCH64_RELATIVE			  1027

/* Section header types */
#define SHT_PROGBITS 1
#define SHT_SYMTAB	 2
#define SHT_STRTAB	 3
#define SHT_RELA	 4
#define SHT_NOBITS	 8

/* Section flags */
#define SHF_WRITE	  0x1
#define SHF_ALLOC	  0x2
#define SHF_EXECINSTR 0x4

/* Symbol binding */
#define ELF64_ST_BIND(info) ((info) >> 4)
#define ELF64_ST_TYPE(info) ((info) & 0xF)
#define STB_GLOBAL			1
#define STB_WEAK			2
#define STT_FUNC			2
#define STT_OBJECT			1
#define SHN_UNDEF			0
#define SHN_ABS				0xfff1

/*
 * Where module image pages get mapped. The kernel image is loaded at
 * 0xFFFFC00000000000 and has grown with the DRM set to a SizeOfImage of
 * 0x14B000 (KernelAA64/KernelAA64.exe); the boot-driver region starts at
 * 0xFFFFC00000400000 (KernelAA64/audrv.c). This base sits at the top of
 * the free band between them -- at its old 0x100000 the image had grown
 * past it, and AuMapPage() refused with "page already present".
 */
#define MOD_VBASE 0xFFFFC000003F0000ULL
static uint64_t mod_vnext = MOD_VBASE;

static uint64_t map_executable_pages(uint64_t phys, size_t size) {
	uint64_t vbase = mod_vnext;
	uint64_t vaddr = vbase;
	uint64_t paddr = phys;
	size_t num_pages = (size + 0xFFF) / 0x1000;

	for (size_t i = 0; i < num_pages; i++) {
		if (!AuMapPage(paddr, vaddr, PTE_NORMAL_MEM)) {
			UARTDebugOut("[mod]: AuMapPage failed\r\n");
			return 0;
		}
		vaddr += 0x1000;
		paddr += 0x1000;
	}

	mod_vnext = (vaddr + 0xFFF) & ~0xFFFULL;
	return vbase;
}

struct kernel_export {
	char* name;
	void* addr;
};
extern struct kernel_export k_exports[];
extern int k_exports_count;

uint64_t k_export_lookup(const char* name) {
	for (int i = 0; i < k_exports_count; i++) {
		if (strcmp(k_exports[i].name, name) == 0)
			return (uint64_t)(uintptr_t)k_exports[i].addr;
	}
	return 0;
}

static int mod_apply_relocations(const uint8_t* base,
								 const Elf64_Shdr* shdrs,
								 Elf64_Half shnum,
								 const Elf64_Sym* syms,
								 const char* strtab,
								 Elf64_Xword num_syms) {
	for (Elf64_Half i = 0; i < shnum; i++) {
		if (shdrs[i].sh_type != SHT_RELA)
			continue;

		Elf64_Half target_idx = shdrs[i].sh_info;
		if (target_idx >= shnum)
			continue;

		uint64_t target_base = shdrs[target_idx].sh_addr;
		if (target_base == 0)
			continue;

		const Elf64_Rela* relas = (const Elf64_Rela*)(base + shdrs[i].sh_offset);
		Elf64_Xword num_relas = shdrs[i].sh_size / sizeof(Elf64_Rela);

		for (Elf64_Xword j = 0; j < num_relas; j++) {
			uint64_t sym_idx = ELF64_R_SYM(relas[j].r_info);
			uint32_t r_type = ELF64_R_TYPE(relas[j].r_info);
			uint64_t loc = target_base + relas[j].r_offset;

			uint64_t sym_val = 0;
			if (sym_idx < num_syms) {
				Elf64_Half shndx = syms[sym_idx].st_shndx;
				if (shndx == SHN_ABS) {
					sym_val = syms[sym_idx].st_value;
				} else if (shndx != SHN_UNDEF) {
					if (shndx >= shnum) {
						UARTDebugOut("[mod]: bad st_shndx in relocation\r\n");
						return -1;
					}
					sym_val = shdrs[shndx].sh_addr + syms[sym_idx].st_value;
				} else {
					const char* name = strtab + syms[sym_idx].st_name;
					sym_val = k_export_lookup(name);
					if (sym_val == 0 && ELF64_ST_BIND(syms[sym_idx].st_info) != STB_WEAK) {
						UARTDebugOut("[mod]: unresolved in relocation: ");
						UARTDebugOut(name);
						UARTDebugOut("\r\n");
						return -1;
					}
				}
			}

			int64_t A = relas[j].r_addend;

			uint64_t page_addr = sym_val + A;
			uint64_t page = page_addr & ~0xFFFULL;
			uint64_t page_offset = page_addr & 0xFFF;

			switch (r_type) {
			case R_AARCH64_NONE:
				break;
			case R_AARCH64_ABS64:
			case R_AARCH64_GLOB_DAT:
			case R_AARCH64_JUMP_SLOT: {
				uint64_t value = sym_val + A;
				*(uint64_t*)loc = value;
				break;
			}
			case R_AARCH64_ABS32: {
				uint64_t value = sym_val + A;
				*(uint32_t*)loc = (uint32_t)value;
				break;
			}
			case R_AARCH64_ABS16: {
				uint64_t value = sym_val + A;
				*(uint16_t*)loc = (uint16_t)value;
				break;
			}
			case R_AARCH64_PREL64:
			case R_AARCH64_PREL32:
			case R_AARCH64_PREL16: {
				uint64_t value = sym_val + A - loc;
				if (r_type == R_AARCH64_PREL64)
					*(uint64_t*)loc = value;
				else if (r_type == R_AARCH64_PREL32)
					*(uint32_t*)loc = (uint32_t)value;
				else
					*(uint16_t*)loc = (uint16_t)value;
				break;
			}
			case R_AARCH64_RELATIVE: {
				*(uint64_t*)loc = (uint64_t)(int64_t)A;
				break;
			}
			case R_AARCH64_ADR_PREL_PG_HI21:
			case R_AARCH64_ADR_PREL_PG_HI21_NC: {
				uint64_t insn = *(uint32_t*)loc;
				int64_t page_off = (int64_t)(page - (loc & ~0xFFFULL));
				int32_t immhi = (int32_t)((page_off >> 12) >> 2);
				int32_t immlo = (int32_t)((page_off >> 12) & 0x3);
				insn = (insn & ~((0x7FFFFULL << 5) | (0x3ULL << 29))) |
					   ((uint64_t)(immhi & 0x7FFFF) << 5) | ((uint64_t)(immlo & 0x3) << 29);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_ADR_PREL_LO21: {
				uint64_t insn = *(uint32_t*)loc;
				int64_t offset = (int64_t)(sym_val + A - loc);
				int32_t imm21 = (int32_t)(offset >> 2);
				int32_t immhi = (imm21 >> 2) & 0x7FFFF;
				int32_t immlo = imm21 & 0x3;
				insn = (insn & ~((0x7FFFFULL << 5) | (0x3ULL << 29))) | ((uint64_t)immhi << 5) |
					   ((uint64_t)immlo << 29);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_ADD_ABS_LO12_NC: {
				uint64_t insn = *(uint32_t*)loc;
				insn = (insn & ~(0xFFFULL << 10)) | ((page_offset & 0xFFF) << 10);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_LDST8_ABS_LO12_NC:
			case R_AARCH64_LDST16_ABS_LO12_NC:
			case R_AARCH64_LDST32_ABS_LO12_NC:
			case R_AARCH64_LDST64_ABS_LO12_NC:
			case R_AARCH64_LDST128_ABS_LO12_NC: {
				uint64_t insn = *(uint32_t*)loc;
				int scale = 0;
				switch (r_type) {
				case R_AARCH64_LDST8_ABS_LO12_NC:
					scale = 0;
					break;
				case R_AARCH64_LDST16_ABS_LO12_NC:
					scale = 1;
					break;
				case R_AARCH64_LDST32_ABS_LO12_NC:
					scale = 2;
					break;
				case R_AARCH64_LDST64_ABS_LO12_NC:
					scale = 3;
					break;
				case R_AARCH64_LDST128_ABS_LO12_NC:
					scale = 4;
					break;
				}
				uint64_t imm12 = (page_offset >> scale) & 0xFFF;
				insn = (insn & ~(0xFFFULL << 10)) | (imm12 << 10);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_LDST_PREL_LO19:
			case R_AARCH64_LD_PREL_LO19: {
				uint64_t insn = *(uint32_t*)loc;
				int64_t offset = (int64_t)(sym_val + A - loc);
				int32_t imm19 = (int32_t)(offset >> 2) & 0x7FFFF;
				insn = (insn & ~(0x7FFFFULL << 5)) | ((uint64_t)imm19 << 5);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_CONDBR19: {
				uint64_t insn = *(uint32_t*)loc;
				int64_t offset = (int64_t)(sym_val + A - loc);
				int32_t imm19 = (int32_t)(offset >> 2) & 0x7FFFF;
				insn = (insn & ~(0x7FFFFULL << 5)) | ((uint64_t)imm19 << 5);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_TSTBR14: {
				uint64_t insn = *(uint32_t*)loc;
				int64_t offset = (int64_t)(sym_val + A - loc);
				int32_t imm14 = (int32_t)(offset >> 2) & 0x3FFF;
				insn = (insn & ~(0x3FFFULL << 5)) | ((uint64_t)imm14 << 5);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_CALL26:
			case R_AARCH64_JUMP26: {
				int64_t offset = (int64_t)(sym_val + A - loc);
				int32_t imm26 = (int32_t)(offset >> 2);
				uint64_t insn = *(uint32_t*)loc;
				insn = (insn & ~0x3FFFFFFULL) | ((uint64_t)(imm26 & 0x3FFFFFF));
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_MOVW_UABS_G0:
			case R_AARCH64_MOVW_UABS_G0_NC:
			case R_AARCH64_MOVW_UABS_G1:
			case R_AARCH64_MOVW_UABS_G1_NC:
			case R_AARCH64_MOVW_UABS_G2:
			case R_AARCH64_MOVW_UABS_G2_NC:
			case R_AARCH64_MOVW_UABS_G3:
			case R_AARCH64_MOVW_SABS_G0:
			case R_AARCH64_MOVW_SABS_G1:
			case R_AARCH64_MOVW_SABS_G2: {
				uint64_t insn = *(uint32_t*)loc;
				int shift = 0;
				switch (r_type) {
				case R_AARCH64_MOVW_UABS_G0:
				case R_AARCH64_MOVW_UABS_G0_NC:
				case R_AARCH64_MOVW_SABS_G0:
					shift = 0;
					break;
				case R_AARCH64_MOVW_UABS_G1:
				case R_AARCH64_MOVW_UABS_G1_NC:
				case R_AARCH64_MOVW_SABS_G1:
					shift = 16;
					break;
				case R_AARCH64_MOVW_UABS_G2:
				case R_AARCH64_MOVW_UABS_G2_NC:
				case R_AARCH64_MOVW_SABS_G2:
					shift = 32;
					break;
				case R_AARCH64_MOVW_UABS_G3:
					shift = 48;
					break;
				}
				uint64_t value = (sym_val + A) >> shift;
				insn = (insn & ~(0xFFFFULL << 5)) | ((value & 0xFFFF) << 5);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			case R_AARCH64_MOVW_PREL_G0:
			case R_AARCH64_MOVW_PREL_G0_NC:
			case R_AARCH64_MOVW_PREL_G1:
			case R_AARCH64_MOVW_PREL_G1_NC:
			case R_AARCH64_MOVW_PREL_G2:
			case R_AARCH64_MOVW_PREL_G2_NC:
			case R_AARCH64_MOVW_PREL_G3: {
				uint64_t insn = *(uint32_t*)loc;
				int shift = 0;
				switch (r_type) {
				case R_AARCH64_MOVW_PREL_G0:
				case R_AARCH64_MOVW_PREL_G0_NC:
					shift = 0;
					break;
				case R_AARCH64_MOVW_PREL_G1:
				case R_AARCH64_MOVW_PREL_G1_NC:
					shift = 16;
					break;
				case R_AARCH64_MOVW_PREL_G2:
				case R_AARCH64_MOVW_PREL_G2_NC:
					shift = 32;
					break;
				case R_AARCH64_MOVW_PREL_G3:
					shift = 48;
					break;
				}
				uint64_t value = (sym_val + A - loc) >> shift;
				insn = (insn & ~(0xFFFFULL << 5)) | ((value & 0xFFFF) << 5);
				*(uint32_t*)loc = (uint32_t)insn;
				break;
			}
			default:
				UARTDebugOut("[mod]: unsupported relocation type\r\n");
				return -1;
			}
		}
	}
	return 0;
}

static void print_hex64(uint64_t v) {
	char buf[20];
	int pos = 0;
	buf[pos++] = '0';
	buf[pos++] = 'x';
	for (int k = 15; k >= 0; k--) {
		int digit = (v >> (k * 4)) & 0xF;
		buf[pos++] = (digit < 10) ? '0' + digit : 'a' + digit - 10;
	}
	buf[pos] = 0;
	UARTDebugOut(buf);
}

int mod_load(const void* data, size_t size, struct mod_handle* handle) {
	memset(handle, 0, sizeof(*handle));

	if (size < sizeof(Elf64_Ehdr)) {
		UARTDebugOut("[mod]: file too small for ELF header\r\n");
		return -1;
	}

	const Elf64_Ehdr* ehdr = (const Elf64_Ehdr*)data;

	if (memcmp(ehdr->e_ident,
			   "\x7f"
			   "ELF",
			   4) != 0) {
		UARTDebugOut("[mod]: not an ELF file\r\n");
		return -1;
	}
	if (ehdr->e_ident[4] != 2) {
		UARTDebugOut("[mod]: not ELF64\r\n");
		return -1;
	}
	if (ehdr->e_type != 1) {
		UARTDebugOut("[mod]: not a relocatable object\r\n");
		return -1;
	}
	if (ehdr->e_machine != 0xB7) {
		UARTDebugOut("[mod]: not AArch64\r\n");
		return -1;
	}

	Elf64_Half shnum = ehdr->e_shnum;
	Elf64_Half shstrndx = ehdr->e_shstrndx;
	const uint8_t* base = (const uint8_t*)data;

	const Elf64_Shdr* shdrs = (const Elf64_Shdr*)(base + ehdr->e_shoff);

	const char* shstrtab = NULL;
	if (shstrndx < shnum) {
		const Elf64_Shdr* strtab_sh = &shdrs[shstrndx];
		shstrtab = (const char*)(base + strtab_sh->sh_offset);
	}

	const Elf64_Shdr* symtab_sh = NULL;
	const Elf64_Shdr* strtab_sh = NULL;

	uint64_t init_addr = 0;
	uint64_t exit_addr = 0;
	Elf64_Half modinfo_idx = 0;

	for (Elf64_Half i = 0; i < shnum; i++) {
		const char* name = (shstrtab && shdrs[i].sh_name) ? shstrtab + shdrs[i].sh_name : "";

		if (shdrs[i].sh_type == SHT_SYMTAB)
			symtab_sh = &shdrs[i];
		else if (strcmp(name, ".init.text") == 0)
			init_addr = shdrs[i].sh_addr;
		else if (strcmp(name, ".exit.text") == 0)
			exit_addr = shdrs[i].sh_addr;
		else if (strcmp(name, ".modinfo") == 0)
			modinfo_idx = i;
	}

	if (symtab_sh && symtab_sh->sh_link < shnum)
		strtab_sh = &shdrs[symtab_sh->sh_link];

	if (!symtab_sh || !strtab_sh) {
		UARTDebugOut("[mod]: missing .symtab or .strtab\r\n");
		return -1;
	}

	const char* strtab = (const char*)(base + strtab_sh->sh_offset);
	const Elf64_Sym* syms = (const Elf64_Sym*)(base + symtab_sh->sh_offset);
	Elf64_Xword num_syms = symtab_sh->sh_size / sizeof(Elf64_Sym);

	size_t total_size = 0;
	for (Elf64_Half i = 0; i < shnum; i++) {
		if (!(shdrs[i].sh_flags & SHF_ALLOC))
			continue;
		if (shdrs[i].sh_type == SHT_NOBITS || shdrs[i].sh_size > 0) {
			total_size = (total_size + shdrs[i].sh_addralign - 1) & ~(shdrs[i].sh_addralign - 1);
			total_size += shdrs[i].sh_size;
		}
	}

	if (total_size == 0) {
		UARTDebugOut("[mod]: no allocatable sections\r\n");
		return -1;
	}

	size_t alloc_size = (total_size + 0xFFF) & ~0xFFFULL;
	if (alloc_size == 0)
		alloc_size = 0x1000;

	uint64_t phys = AuPmmngrAllocPages(alloc_size / 0x1000, 1, 0, AURORA_PAGE_KERNEL);
	if (!phys) {
		UARTDebugOut("[mod]: allocation failed\r\n");
		return -1;
	}

	uint64_t virt = map_executable_pages(phys, alloc_size);
	if (!virt) {
		AuPmmngrReleasePages(phys);
		return -1;
	}
	memset((void*)virt, 0, alloc_size);

	uint64_t offset = 0;
	for (Elf64_Half i = 0; i < shnum; i++) {
		if (!(shdrs[i].sh_flags & SHF_ALLOC))
			continue;

		Elf64_Xword align = shdrs[i].sh_addralign;
		if (align > 0)
			offset = (offset + align - 1) & ~(align - 1);

		((Elf64_Shdr*)shdrs)[i].sh_addr = virt + offset;

		if (shdrs[i].sh_type != SHT_NOBITS && shdrs[i].sh_size > 0) {
			memcpy((void*)(virt + offset), base + shdrs[i].sh_offset, shdrs[i].sh_size);
		}

		offset += shdrs[i].sh_size;
	}

	handle->base = virt;
	handle->phys = phys;
	handle->size = alloc_size;

	for (Elf64_Xword i = 0; i < num_syms; i++) {
		if (syms[i].st_shndx != SHN_UNDEF)
			continue;
		if (syms[i].st_name == 0)
			continue;

		const char* sym_name = strtab + syms[i].st_name;
		uint64_t addr = k_export_lookup(sym_name);

		if (addr == 0) {
			UARTDebugOut("[mod]: unresolved symbol: ");
			UARTDebugOut(sym_name);
			UARTDebugOut("\r\n");
			if (ELF64_ST_BIND(syms[i].st_info) != STB_WEAK) {
				AuPmmngrReleasePages(phys);
				return -1;
			}
		}
	}

	if (mod_apply_relocations(base, shdrs, shnum, syms, strtab, num_syms) < 0) {
		AuPmmngrReleasePages(phys);
		return -1;
	}

	/* sections were copied and relocated through the D-cache: make the
	 * module's code visible to instruction fetch before init runs */
	AA64SyncICacheRange((void*)virt, alloc_size);

	for (Elf64_Xword i = 0; i < num_syms; i++) {
		if (syms[i].st_name == 0)
			continue;
		const char* name = strtab + syms[i].st_name;
		if (strcmp(name, "init_module") == 0 || strcmp(name, "init") == 0 ||
			strcmp(name, "module_init") == 0) {
			if (syms[i].st_shndx != SHN_UNDEF && syms[i].st_shndx < shnum) {
				init_addr = shdrs[syms[i].st_shndx].sh_addr + syms[i].st_value;
			}
		}
		if (strcmp(name, "cleanup_module") == 0 || strcmp(name, "exit") == 0 ||
			strcmp(name, "module_exit") == 0) {
			if (syms[i].st_shndx != SHN_UNDEF && syms[i].st_shndx < shnum) {
				exit_addr = shdrs[syms[i].st_shndx].sh_addr + syms[i].st_value;
			}
		}
	}

	strncpy(handle->name, "module", MOD_MAX_NAME - 1);
	handle->name[MOD_MAX_NAME - 1] = 0;
	if (modinfo_idx && modinfo_idx < shnum && (shdrs[modinfo_idx].sh_flags & SHF_ALLOC) &&
		shdrs[modinfo_idx].sh_addr != 0) {
		const char* mi = (const char*)shdrs[modinfo_idx].sh_addr;
		Elf64_Xword n = shdrs[modinfo_idx].sh_size;
		Elf64_Xword i = 0;
		while (i + 5 <= n) {
			if (mi[i] == 'n' && memcmp(mi + i, "name=", 5) == 0) {
				i += 5;
				size_t k = 0;
				while (i + k < n && mi[i + k] && k < MOD_MAX_NAME - 1) {
					handle->name[k] = mi[i + k];
					k++;
				}
				handle->name[k] = 0;
				break;
			}
			while (i < n && mi[i])
				i++;
			i++;
		}
	}

	handle->init_func = init_addr;
	handle->exit_func = exit_addr;
	handle->loaded = 1;

	UARTDebugOut("[mod]: .text after reloc: ");
	{
		const uint8_t* code = (const uint8_t*)virt;
		char buf[4];
		for (int k = 0; k < 32; k++) {
			int hi = (code[k] >> 4) & 0xF;
			int lo = code[k] & 0xF;
			buf[0] = (hi < 10) ? '0' + hi : 'a' + hi - 10;
			buf[1] = (lo < 10) ? '0' + lo : 'a' + lo - 10;
			buf[2] = ' ';
			buf[3] = 0;
			UARTDebugOut(buf);
		}
	}
	UARTDebugOut("\r\n");

	UARTDebugOut("[mod]: loaded, base=");
	print_hex64(virt);
	UARTDebugOut("\r\n");

	return 0;
}

void mod_unload(struct mod_handle* handle) {
	if (!handle->loaded)
		return;

	AuPmmngrReleasePages(handle->phys);
	handle->loaded = 0;
	handle->base = 0;
	handle->phys = 0;
	handle->size = 0;
}

int mod_call_init(struct mod_handle* handle) {
	if (!handle->loaded || handle->init_func == 0)
		return -1;

	UARTDebugOut("[mod]: calling init at ");
	print_hex64(handle->init_func);
	UARTDebugOut("\r\n");

	typedef int (*init_fn)(void);
	init_fn fn = (init_fn)handle->init_func;
	return fn();
}

void mod_call_exit(struct mod_handle* handle) {
	if (!handle->loaded || handle->exit_func == 0)
		return;

	typedef void (*exit_fn)(void);
	exit_fn fn = (exit_fn)handle->exit_func;
	fn();
}
