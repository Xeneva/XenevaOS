# XenevaOS Doomgeneric Makefile (GCC / LLVM Build)
# Builds the FULL Doom engine + XenevaOS platform layer (doomgeneric_xe.cpp).

# Default toolchain is LLVM (matches all other apps; the GNU-ld PE link
# misresolves branch relocations, e.g. printf -> garbage).
TOOLCHAIN ?= llvm

# Target binary (8.3 name: the guest FAT driver has no LFN support)
TARGET = doom.exe
CROSS_COMPILE ?= aarch64-linux-gnu-

# Include paths (local dir first so Doom headers shadow libc ones)
INCLUDES = -I. -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr -I../../Libs/Chitralekha -I../../Ports/freetype2/include

# All Doom engine C sources present in this dir (doomgeneric.c included;
# w_file_stdc.c is the XenevaOS stdio WAD backend). main/doomgeneric_xe are C++.
C_SRCS = $(wildcard *.c)
CPP_SRCS = main.cpp doomgeneric_xe.cpp
S_SRCS = crt_arm64.s

# Object directory
OBJDIR = obj
C_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))
OBJS = $(C_OBJS) $(CPP_OBJS) $(S_OBJS)

# Target ELF
TARGET_ELF = $(OBJDIR)/doomgeneric.elf

# Common defines:
#  __bool_true_false_are_defined -- XEClib's stdbool defines true/false macros;
#    this makes doomtype.h take the `typedef unsigned int boolean` path instead
#    of re-declaring false/true as enum constants (hard error).
COMMON_DEFS = -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -D__bool_true_false_are_defined -DCOLOR_RGBA

# Toolchain configuration
ifeq ($(TOOLCHAIN), llvm)
    CXX = clang++
    CC  = clang
    LD  = clang++
    LLVM_TARGET = -target aarch64-unknown-windows
    CFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
               $(COMMON_DEFS) \
               -U_WIN32 \
               -include stdbool.h \
               -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
               -Wno-macro-redefined \
               $(INCLUDES)
    CXXFLAGS = $(CFLAGS) -std=c++17
    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native, -Wl,-filealign:0x1000
else
    # GCC configuration
    CXX = $(CROSS_COMPILE)g++
    CC  = $(CROSS_COMPILE)gcc
    LD  = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy

    CFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fPIE -fno-exceptions -fno-rtti \
               $(COMMON_DEFS) \
               -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
               -Wno-macro-redefined -Wno-unused-parameter -Wno-missing-field-initializers \
               $(INCLUDES)
    CXXFLAGS = $(CFLAGS) -std=c++17
    LDFLAGS = -nostdlib -T ../../Process/XELnch/linker.ld -L../../Libs/XEClib -L../../Libs/Chitralekha
endif

# Build rules
.PHONY: all clean llvm

all: $(TARGET)

llvm:
	@$(MAKE) TOOLCHAIN=llvm all

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Wno-unused-command-line-argument -c $< -o $@

$(TARGET_ELF): $(OBJS)
	@echo "Linking doomgeneric.elf..."
ifeq ($(TOOLCHAIN), llvm)
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/Chitralekha/libChitralekha.a ../../Libs/XEClib/libXEClib.a -o $@
else
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/Chitralekha/libChitralekha.a ../../Libs/XEClib/libXEClib.a -o $@
endif

$(TARGET): $(TARGET_ELF)
	@echo "Converting ELF to PE32+ ($@)..."
ifeq ($(TOOLCHAIN), llvm)
	cp -f $(TARGET_ELF) $@
else
	$(OBJCOPY) -O pei-aarch64-little $(TARGET_ELF) $@
endif

clean:
	rm -rf $(OBJDIR) $(TARGET) $(TARGET_ELF) doom.exe doomgeneric.exe doomgeneric.elf
