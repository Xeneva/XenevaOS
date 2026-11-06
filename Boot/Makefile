# Target EFI binary
TARGET_EFI = BOOTx64.efi
TARGET_SO = BOOTx64.so

# Setting OBJCOPY
OBJCOPY = objcopy

# Source files
CPP_SRCS = clib.cpp file.cpp mem.cpp paging.cpp pe.cpp physm.cpp video.cpp xnldr.cpp xnout.cpp
ASM_SRCS = lowlevel.asm

# Object directory
OBJDIR = obj

# GNU-EFI Source Directory (Override via env variable or make argument)
GNU_EFI_DIR ?= ../gnu-efi

# Include directories
INCLUDES = -I../BaseHdr -Iinclude -Iinclude/x64 -I$(GNU_EFI_DIR)/inc

# Compiler flags
CXXFLAGS = -Wno-error=pragmas -mno-red-zone -mno-avx -fPIE \
		    -Wall -Wextra -Werror -funsigned-char -fshort-wchar \
		   -fno-strict-aliasing -ffreestanding -fno-stack-protector -fno-stack-check \
		   -fno-merge-all-constants -std=c++17 -DGNU_EFI_USE_MS_ABI -maccumulate-outgoing-args -DGNU_EFI_USE_MS_ABI $(INCLUDES)

# Detect OS for NASM format
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    NASMFLAGS = -f elf64
else
    NASMFLAGS = -f win64
endif

# GNU-EFI link setup
LDSCRIPT = ${GNU_EFI_DIR}/gnuefi/elf_x86_64_efi.lds
CRT0 = ${GNU_EFI_DIR}/x86_64/gnuefi/crt0-efi-x86_64.o
LIBS = -lgnuefi -lefi
LIBDIRS = -L${GNU_EFI_DIR}/x86_64/gnuefi -L${GNU_EFI_DIR}//x86_64/lib

# Tools
CXX = g++
NASM = nasm
LD = ld
CP = cp

# Output paths
SO_OUTPUT  = $(TARGET_SO)
EFI_OUTPUT = Build/EFI/BOOT/$(TARGET_EFI)
COPY_DEST  = $(XENEVA_BUILDS)/EFI/BOOT/$(TARGET_EFI)

# Object files
OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS)) $(OBJDIR)/lowlevel.o

.PHONY: all clean

all: $(EFI_OUTPUT)

# Link x64 EFI binary
$(SO_OUTPUT): $(CRT0) $(OBJS)
	# Added -z noexecstack to suppress irrelevant GNU-stack warning from NASM ELF64 objects
	$(LD) -nostdlib --warn-common --no-undefined --fatal-warnings --build-id=sha1 \
	-z noexecstack -z nocombreloc -shared -Bsymbolic $(LIBDIRS) -T$(LDSCRIPT) $(CRT0) $(OBJS) -o $@ $(LIBS)

$(EFI_OUTPUT): $(SO_OUTPUT)
	@mkdir -p $(dir $@)
	$(OBJCOPY) -j .text -j .sdata -j .data -j .rodata -j .dynamic -j .dynsym -j .rel -j .rela \
			   -j .rel.* -j .rela.* \
	           -j .reloc --target=efi-app-x86_64 --subsystem=10 $< $@
# $(CP) $@ $(COPY_DEST)

# Compile C++ source files
$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Assemble NASM source
$(OBJDIR)/lowlevel.o: lowlevel.asm
	@mkdir -p $(OBJDIR)
	$(NASM) $(NASMFLAGS) -o $@ $<

clean:
	rm -rf $(OBJDIR) Build $(SO_OUTPUT) $(EFI_OUTPUT)

