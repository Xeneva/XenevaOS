TARGET = virtnet.dll
SRC = virtionet.cpp

CXX = clang++
TARGET_TRIPLE = -target aarch64-unknown-windows

#Root include directories
INCLUDES = -I../../../BaseHdr 

KERNEL_LIB = ../../../KernelAA64/KernelAA64.lib 

CXXFLAGS = -Wall -Wextra -O2 -ffreestanding -fno-builtin \
           -nostdinc -DARCH_ARM64 $(INCLUDES) $(TARGET_TRIPLE)

#Linker flags for freestanding PE/COFF DLL
LDFLAGS = -shared -fuse-ld=lld -nostdlib $(TARGET_TRIPLE) \
          -Wl,-noentry \
		  -Wl,-force:unresolved

.PHONY: all clean

all: $(TARGET)

$(TARGET):$(SRC)
	$(CXX) $(CXXFLAGS) $(SRC) $(LDFLAGS) $(KERNEL_LIB) -o $@

clean:
	rm -f $(TARGET)