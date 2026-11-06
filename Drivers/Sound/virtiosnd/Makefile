# XenevaOS virtio-snd Driver Makefile (GCC / LLVM Build)

TARGET = virtsnd.dll
CPP_SRCS = main.cpp
OBJDIR = obj/$(TOOLCHAIN)
INCLUDES = -I../../../BaseHdr
KERNEL_LIB = ../../../KernelAA64/KernelAA64.lib

# Default toolchain is GCC. Bare `make` still hits the llvm: rule first,
# matching BootAA64/KernelAA64/Process/* so clangd (`make llvm`) and
# `make TOOLCHAIN=gcc all` both work.
TOOLCHAIN ?= gcc
CROSS_COMPILE ?= aarch64-linux-gnu-

CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
OBJS = $(CPP_OBJS)

.PHONY: all clean llvm
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

ifeq ($(TOOLCHAIN), llvm)
    CXX = clang++
    LLVM_TARGET = -target aarch64-unknown-windows

    CXXFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-builtin \
               -fno-exceptions -fno-rtti -nostdinc -DARCH_ARM64 \
               $(INCLUDES) $(LLVM_TARGET)

    LDFLAGS = -shared -fuse-ld=lld -nostdlib $(LLVM_TARGET) \
              -Wl,-noentry \
              -Wl,-force:unresolved

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) $(LDFLAGS) $(KERNEL_LIB) -o $@
else
    CXX = $(CROSS_COMPILE)g++

    CXXFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-builtin \
               -fno-exceptions -fno-rtti -nostdinc -DARCH_ARM64 \
               -fpermissive -Wno-changes-meaning \
               $(INCLUDES)

    # GCC compiles the driver for syntax/clangd checks. The PE/COFF
    # DLL the kernel loads is produced by the LLVM path (`make llvm`).
all: $(OBJS)
endif

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf obj $(TARGET)
