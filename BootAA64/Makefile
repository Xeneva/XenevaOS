# Target EFI binary
TARGET_EFI = BOOTAA64.efi
TARGET_SO = BOOTAA64.so

# MSYS2 shells (MINGW64/CLANG64/UCRT64/MSYS/...) set $MSYSTEM. There's no
# aarch64-linux-gnu-gcc cross toolchain packaged for MSYS2, so default to
# llvm there; everywhere else (Linux) keeps defaulting to gcc.
ifdef MSYSTEM
    TOOLCHAIN ?= llvm
endif
TOOLCHAIN ?= gcc

BLEED ?= 0
ifeq ($(BLEED),1)
    BLEED_FLAGS = -D__XENEVA_BLEED__ -O2
endif

# Triggered when you run "make llvm"
.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

# Source files
CPP_SRCS = clib.cpp devicetree.cpp file.cpp mem.cpp paging.cpp pe.cpp physm.cpp uart0.cpp vector.cpp video.cpp xnldr.cpp xnout.cpp
ASM_SRCS = lowlevel.s vectoraa64.s

# Object directory
OBJDIR = obj

# GNU-EFI Source Directory (Override via env variable or make argument)
GNU_EFI_DIR ?= ../gnu-efi

# Include directories
INCLUDES = -I../BaseHdr -I../Boot/include -I../Boot/include/AArch64 -I$(GNU_EFI_DIR)/inc -I$(GNU_EFI_DIR)/inc/aarch64

# Target Board selection (Default: qemu_virt)
BOARD ?= qemu_virt
ifeq ($(BOARD), rpi3)
	BOARD_FLAG = -D__TARGET_BOARD_RPI3__
else
	BOARD_FLAG = -D__TARGET_BOARD_QEMU_VIRT__
endif

# Output paths
SO_OUTPUT  = $(TARGET_SO)
EFI_OUTPUT = Build/EFI/BOOT/$(TARGET_EFI)

# Object files
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
ASM_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(ASM_SRCS))
OBJS = $(CPP_OBJS) $(ASM_OBJS)

ifeq ($(TOOLCHAIN),llvm)

#*	llvm configuration
	CC		=clang
	CXX		=clang++

	LLVM_TARGET = -target aarch64-unknown-windows

	CXXFLAGS = $(LLVM_TARGET) -Wno-error=pragmas \
               -Wall -Wextra -funsigned-char -fshort-wchar \
               -ffreestanding -fno-stack-protector \
               -std=c++17 $(BLEED_FLAGS) $(BOARD_FLAG) $(INCLUDES)

	LDFLAGS  = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:efi_main -Wl,-subsystem:efi_application

ifeq ($(shell command -v $(CXX) 2>/dev/null),)
    $(error $(CXX) not found on PATH. On MSYS2: pacman -S mingw-w64-clang-x86_64-toolchain (or the clangarm64/ucrt64 package matching your shell), then reopen the shell)
endif
ifeq ($(shell command -v ld.lld 2>/dev/null),)
    $(error ld.lld not found on PATH. It ships with the clang toolchain package above)
endif

else

#* original gcc configuration
ifdef MSYSTEM
    $(error TOOLCHAIN=gcc needs aarch64-linux-gnu-gcc/g++, which MSYS2 doesn't package. Run "make llvm" instead (MSYS2 already defaults to it))
endif

	CC      = aarch64-linux-gnu-gcc
    CXX     = aarch64-linux-gnu-g++
    LD      = aarch64-linux-gnu-ld
    OBJCOPY = aarch64-linux-gnu-objcopy

    CXXFLAGS = -Wno-error=pragmas -fPIE \
               -Wall -Wextra -funsigned-char -fshort-wchar \
               -fno-strict-aliasing -ffreestanding -fno-stack-protector -fno-stack-check \
               -fno-merge-all-constants -std=c++17 -DGNU_EFI_USE_MS_ABI $(BLEED_FLAGS) $(BOARD_FLAG) $(INCLUDES)

    LDSCRIPT = ${GNU_EFI_DIR}/gnuefi/elf_aarch64_efi.lds
    CRT0     = ${GNU_EFI_DIR}/aarch64/gnuefi/crt0-efi-aarch64.o
    LIBS     = -lgnuefi -lefi
    LIBDIRS  = -L${GNU_EFI_DIR}/aarch64/gnuefi -L${GNU_EFI_DIR}/aarch64/lib
endif

# build rules
all: $(EFI_OUTPUT)

ifeq ($(TOOLCHAIN), llvm)
$(EFI_OUTPUT): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $(OBJS) -o $@
else
# Original GCC compilation
$(SO_OUTPUT): $(CRT0) $(OBJS)
	# clib.o and gnu-efi's libefi.a both define memset/memcpy, and that's
	# always a hard link error, not a -fno-common thing. clib.o links before
	# the archive so its copy wins; I just needed to tell ld to allow it
	# instead of aborting on the duplicate --axiss
	$(LD) -nostdlib --warn-common --no-undefined --allow-multiple-definition --build-id=sha1 -z noexecstack -z max-page-size=4096 \
	-shared -Bsymbolic $(LIBDIRS) -T$(LDSCRIPT) $(CRT0) $(OBJS) -o $@ $(LIBS)

$(EFI_OUTPUT): $(SO_OUTPUT)
	@mkdir -p $(dir $@)
	# used to pass --target=efi-app-aarch64 here but this binutils (2.47)
	# doesn't ship that BFD target at all, objcopy just said "file format
	# not recognized". pei-aarch64-little is the real one for aarch64 here
	# (same target KernelAA64/Makefile already uses) --axiss
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic -j .rodata -j .rel -j .rela \
	           -j .rel.* -j .rela.* -j .rel* -j .rela* -j .areloc \
	           -j .reloc -O pei-aarch64-little --subsystem=10 $< $@
endif

# Compile C++ source files
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Assemble ASM source files
$(OBJDIR)/%.o: %.s
	@mkdir -p $(OBJDIR)
	$(CC) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) Build $(TARGET_SO)
