# Raspberry Pi 3B+ on-board Bluetooth driver (rpibt.dll)
#
# Brings the CYW43455's Bluetooth controller up over the PL011 (GPIO30-33,
# RTS/CTS): power through the firmware GPIO expander, Broadcom patch RAM
# download from /bt43455.hcd, then hands the UART to the kernel host stack
# (KernelAA64/Bt/host.c). Built against the BOARD=rpi3 kernel's import
# library; the driver manager loads it from /board.cnf
# (Scripts/Linux/build_rpi3.sh).

TARGET = rpibt.dll
SRCS = rpi_bt.cpp
OBJS = $(SRCS:.cpp=.o)

CXX = clang++
TARGET_TRIPLE = -target aarch64-unknown-windows

INCLUDES = -I../../../BaseHdr
KERNEL_LIB = ../../../KernelAA64/KernelAA64.lib

CXXFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-builtin -nostdinc \
           -fno-exceptions -fno-rtti -mgeneral-regs-only \
           -DARCH_ARM64 -D__TARGET_BOARD_RPI3__ $(INCLUDES) $(TARGET_TRIPLE)

LDFLAGS = -shared -fuse-ld=lld -nostdlib $(TARGET_TRIPLE) \
          -Wl,-noentry \
          -Wl,-force:unresolved

.PHONY: all clean llvm

all: $(TARGET)

llvm: all

$(TARGET): $(OBJS)
	$(CXX) $(LDFLAGS) $(OBJS) $(KERNEL_LIB) -o $@

%.o: %.cpp rpi_bt.h Makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) rpibt.lib
