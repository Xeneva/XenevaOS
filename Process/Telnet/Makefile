# XenevaOS Telnet Makefile (LLVM Build, AArch64)

TOOLCHAIN ?= llvm

TARGET = telnet.exe
CROSS_COMPILE ?= aarch64-linux-gnu-

INCLUDES = -I. -I../../Libs/XEClib/includes -I../../Libs/XEClib/includes/c++ -I../../BaseHdr

CPP_SRCS = main.cpp

OBJDIR = obj
CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
CRT_OBJ = $(OBJDIR)/crt0_arm64.o
OBJS = $(CPP_OBJS) $(CRT_OBJ)

COMMON_DEFS = -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -D__bool_true_false_are_defined

ifeq ($(TOOLCHAIN), llvm)
    CXX = clang++
    CC  = clang
    LD  = clang++
    LLVM_TARGET = -target aarch64-unknown-windows
    CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
               -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
               $(COMMON_DEFS) \
               -U_WIN32 \
               -include stdbool.h \
               -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
               -Wno-implicit-function-declaration -Wno-c++11-narrowing \
               -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
               -Wno-macro-redefined \
               $(INCLUDES)
    LDFLAGS = $(LLVM_TARGET) -fuse-ld=lld -nostdlib -Wl,-entry:_aumain -Wl,-base:0x600000000 -Wl,-subsystem:native 
endif

.PHONY: all clean llvm

all: $(TARGET)

llvm:
	@$(MAKE) TOOLCHAIN=llvm all

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(CRT_OBJ): ../../Process/Init/crt0arm64.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

$(TARGET): $(OBJS)
	@echo "Linking $@ (LLVM)..."
	$(LD) $(LDFLAGS) $(OBJS) ../../Libs/XEClib/libXEClib.a -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
