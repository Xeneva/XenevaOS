# Synopsys DWC2 USB host driver for the Raspberry Pi 3B/3B+ (dwc2.dll)
#
# Built against the BOARD=rpi3 kernel's import library; the driver manager
# loads it from /board.cnf on the Pi image (Scripts/Linux/build_rpi3.sh).

TARGET = dwc2.dll
SRCS = main.cpp dwc2_hcd.cpp usb_enum.cpp usb_hid.cpp
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

%.o: %.cpp dwc2_hw.h usb.h usb_hid.h Makefile
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS) dwc2.lib
