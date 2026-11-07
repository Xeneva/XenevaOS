TARGET = wifi.dll
SRC = wifi.cpp crypto.c

CXX = clang++
CC = clang
TARGET_TRIPLE = -target aarch64-unknown-windows

INCLUDES = -I../../../BaseHdr

KERNEL_LIB = ../../../KernelAA64/KernelAA64.lib

CFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-builtin \
         -nostdinc -DARCH_ARM64 $(INCLUDES) $(TARGET_TRIPLE)

CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti

LDFLAGS = -shared -fuse-ld=lld -nostdlib $(TARGET_TRIPLE) \
          -Wl,-noentry \
          -Wl,-force:unresolved

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -c crypto.c -o crypto.o
	$(CXX) $(CXXFLAGS) -c wifi.cpp -o wifi.o
	$(CXX) $(LDFLAGS) wifi.o crypto.o $(KERNEL_LIB) -o $@

clean:
	rm -f $(TARGET) crypto.o wifi.o
