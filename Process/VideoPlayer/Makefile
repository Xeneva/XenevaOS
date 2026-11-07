# XenevaOS VideoPlayer Makefile (GCC / LLVM Build)

# Default toolchain is GCC
TOOLCHAIN ?= gcc

TARGET = vidplr.elf
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr -I../../Libs/Chitralekha -I../../Ports/freetype2/include \
-Ih264bsd -Ilibhelix-aac -Iutils

CPP_SRCS = $(shell find . -name '*.cpp')
C_SRCS = $(shell find . -name '*.c')
S_SRCS = crt_arm64.s

OBJDIR = obj
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
C_OBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(C_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))
OBJS = $(CPP_OBJS) $(C_OBJS) $(S_OBJS)

ifeq ($(TOOLCHAIN), llvm)
    # LLVM / Clang configuration
    CXX = clang++
    CC  = clang
    LD  = clang++

    LLVM_TARGET = -target aarch64-unknown-windows

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -nostdlibinc -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT -DUSE_DEFAULT_STDLIB \
               -DXXXX__arm__ -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               $(INCLUDES)

    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native -Wl,-filealign:0x1000 \
              -Wl,-map:vidplr.map
else
    # GCC configuration
    CXX = $(CROSS_COMPILE)g++
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fPIE -fno-exceptions -fno-rtti \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT \
               $(INCLUDES)
endif

# Triggered when you run "make llvm"
.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

all: vidplr.exe

ifeq ($(TOOLCHAIN), llvm)
vidplr.exe: $(OBJS)
	@echo "Linking vidplr.exe (LLVM)..."
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/Chitralekha/libChitralekha.a ../../Libs/XEClib/libXEClib.a -o $@
else
vidplr.exe: $(TARGET)
	@echo "Converting ELF to PE32+ (vidplr.exe)..."
	$(OBJCOPY) -O pei-aarch64-little $(TARGET) audplr.exe

$(TARGET): $(OBJS)
	@echo "Linking vidplr.elf..."
	$(LD) -nostdlib -T linker.ld $(OBJS) ../../Libs/Chitralekha/libChitralekha.a ../../Libs/XEClib/libXEClib.a -o $@
endif

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Compile C source files
$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) vidplr.exe $(TARGET)
