# XenevaOS init.exe Makefile (GCC/LLVM Build)

TARGET = xeldr.elf
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -I../Libs/XEClib/includes -I../Libs/XEClib/includes/c++ 
OBJDIR = obj

BLEED ?= 0
ifeq ($(BLEED),1)
    BLEED_FLAGS = -D__XENEVA_BLEED__
endif

CPP_SRCS = $(shell find . -name '*.cpp')
S_SRCS = $(shell find . -name '*.s')

CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))

OBJS = $(CPP_OBJS) $(S_OBJS)

.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

ifeq ($(TOOLCHAIN), llvm)
    CXX = clang++
    CC = clang
    LD = clang++
    OBJCOPY = llvm-objcopy
    LLVM_TARGET = -target aarch64-unknown-windows

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 $(LLVM_TARGET) \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS $(BLEED_FLAGS) \
               -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               $(INCLUDES)

    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native, -Wl,-map:XELoader.map

    all: xeldr.exe

    xeldr.exe: $(OBJS)
	@echo "Linking init.exe (LLVM)..."
	$(LD) $(LDFLAGS) $(OBJS) ../Libs/XEClib/libXEClib.a -o $@

    $(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

    $(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -c $< -o $@

    clean:
	rm -rf $(OBJDIR) xeldr.exe
else
    CXX = $(CROSS_COMPILE)g++
    CC = $(CROSS_COMPILE)gcc
    LD = $(CROSS_COMPILE)ld
    OBJCOPY = $(CROSS_COMPILE)objcopy

    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fPIE \
               -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS $(BLEED_FLAGS) \
               $(INCLUDES)

    all: xeldr.exe

    init.exe: $(TARGET)
	@echo "Converting ELF to PE32+ (xeldr.exe)..."
	$(OBJCOPY) -O pei-aarch64-little $(TARGET) init.exe

    $(TARGET): $(OBJS)
	@echo "Linking xeldr.elf..."
	$(LD) -nostdlib -T linker.ld $(OBJS) ../../Libs/XEClib/libXEClib.a -o $@

    $(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

    $(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -c $< -o $@

    clean:
	rm -rf $(OBJDIR) $(TARGET) xeldr.exe
endif
