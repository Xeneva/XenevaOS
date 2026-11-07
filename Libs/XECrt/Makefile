TOOLCHAIN ?= gcc
TARGET = xecrt.a

ifeq ($(TOOLCHAIN), llvm)
	# LLVM / Clang configuration
	CXX = clang++
	CC  = clang
	AR  = llvm-ar

	LLVM_TARGET = -target aarch64-unknown-windows

	CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check \
	           -fno-strict-aliasing -fno-rtti -fno-exceptions -O2 $(LLVM_TARGET) \
	           -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS \
	           -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
	           -Wno-implicit-function-declaration -Wno-c++11-narrowing \
	           $(INCLUDES)

else
	# GCC configuration
	CROSS_COMPILE ?= aarch64-linux-gnu-
	CXX = $(CROSS_COMPILE)g++
	CC  = $(CROSS_COMPILE)gcc
	AR  = $(CROSS_COMPILE)ar

	CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
	           -O2 -fPIE \
	           -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS \
	           $(INCLUDES)
endif

OBJDIR = obj

# Source files for ARM64
CPP_SRCS = crt.cpp

S_SRCS = crt0_arm64.s

CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))

OBJS = $(CPP_OBJS) $(S_OBJS)

# Triggered when you run "make llvm"
.PHONY: all clean llvm 
llvm:
	@$(MAKE) TOOLCHAIN=llvm all

all: $(TARGET)

$(TARGET): $(OBJS)
	@echo "Archiving $(TARGET)..."
	$(AR) rcs $@ $(OBJS)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
