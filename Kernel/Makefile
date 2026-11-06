# XenevaOS Kernel Makefile (GCC Build)

# Target
TARGET_KERNEL = Kernel.elf

# Object directory
OBJDIR = obj

# Compiler and Flags
CXX = g++
INCLUDES = -I../BaseHdr -I. -I../KernelAA64
# Note: Kernel requires strict higher-half memory flags and no-red-zone
CXXFLAGS = -Wno-error=pragmas -mno-red-zone -mcmodel=large -fPIE \
           -Wall -Wextra -funsigned-char -fshort-wchar -fno-strict-aliasing \
           -ffreestanding -fno-stack-protector -fno-stack-check -std=c++17 -DARCH_X64 $(INCLUDES)

# Find all C++ source files in the Kernel directory
CPP_SRCS = $(wildcard *.cpp) $(wildcard x64/*.cpp)
OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))

.PHONY: all clean

all: $(TARGET_KERNEL)

$(TARGET_KERNEL): $(OBJS)
	@echo "Linking Kernel..."
	# TODO: We need a linker script (linker.ld) to place the kernel at higher-half memory
	# $(LD) -nostdlib -T linker.ld $(OBJS) -o $@

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET_KERNEL)
