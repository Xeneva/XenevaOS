# XenevaOS Chitralekha Library Makefile (GCC / LLVM Build)

DLLTARGET = chlkha.dll
IMPLIB = libChitralekha.a
DEFFILE = Chitralekha.def
TARGET = libChitralekha.a

XECLIB_PATH = ../XEClib/libXEClib.a 
XECRT_PATH = ../XECrt/xecrt.lib
FREETYPE_PATH = ftype.lib

LIBS = $(XECLIB_PATH) $(XECRT_PATH) $(FREETYPE_PATH)

# Default toolchain is GCC
TOOLCHAIN ?= gcc

# Triggered when you run "make llvm"
.PHONY: all clean llvm dll dll-internal
llvm:
	@$(MAKE) TOOLCHAIN=llvm all
dll:
	@$(MAKE) TOOLCHAIN=llvm dll-internal

INCLUDES = -I./ -I../XEClib/includes -I../../BaseHdr -I../XEClib/includes/c++ -I../../Ports/freetype2/include

BLEED ?= 0
ifeq ($(BLEED),1)
	BLEED_FLAGS = -D__XENEVA_BLEED__
endif

UNIKERNEL ?= 0
ifeq ($(UNIKERNEL),1)
	UNIKERNEL_FLAGS = -D__XENEVA_UNIKERNEL__
endif

ifeq ($(TOOLCHAIN), llvm)
	# LLVM / Clang configuration
	CXX = clang++
	CC  = clang
	AR  = llvm-ar

	LLVM_TARGET = -target aarch64-unknown-windows

	CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check \
	           -fno-strict-aliasing -O2 -fno-exceptions -fno-rtti $(LLVM_TARGET) \
	           -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT -DCOLOR_RGBA $(BLEED_FLAGS) $(UNIKERNEL_FLAGS) \
	           -Wno-error=incompatible-pointer-types -Wno-error=int-conversion \
	           -Wno-implicit-function-declaration -Wno-c++11-narrowing \
	           $(INCLUDES)

dll-internal: $(DLLTARGET)

else
	# GCC configuration
	CROSS_COMPILE ?= aarch64-linux-gnu-
	CXX = $(CROSS_COMPILE)g++
	CC  = $(CROSS_COMPILE)gcc
	AR  = $(CROSS_COMPILE)ar

	CXXFLAGS = -Wall -Wextra -ffreestanding -fno-stack-protector -fno-stack-check -fno-strict-aliasing \
	           -O2 -fPIE -fno-exceptions -fno-rtti \
	           -DARCH_ARM64 -D__TARGET_BOARD_QEMU_VIRT__ -D_USE_DLMALLOC -D__STDC_LIMIT_MACROS -DTHEME_DEFAULT -DCOLOR_RGBA $(BLEED_FLAGS) $(UNIKERNEL_FLAGS) \
	           $(INCLUDES)
endif

OBJDIR = obj

# Source files for ARM64
CPP_SRCS = audio/audio.cpp \
           boxblur.cpp \
           chitralekha.cpp \
           color.cpp \
           draw.cpp \
           font.cpp \
           keycode.cpp \
           ttf.cpp \
           widgets/base.cpp \
           widgets/button.cpp \
           widgets/defaultmenu.cpp \
           widgets/defaultonoff.cpp \
           widgets/defaultprogressbar.cpp \
           widgets/defaultscrollbar.cpp \
           widgets/defaultslider.cpp \
           widgets/defaulttextbox.cpp \
           widgets/defaultviews.cpp \
           widgets/defaultwindow.cpp \
           widgets/flatbutton.cpp \
           widgets/flatsidebar.cpp \
           widgets/gmstyle.cpp \
           widgets/gridview.cpp \
           widgets/icon.cpp \
           widgets/list.cpp \
           widgets/listview.cpp \
           widgets/menubar.cpp \
           widgets/menu.cpp \
           widgets/msgbox.cpp \
           widgets/onoff.cpp \
           widgets/popupwin.cpp \
           widgets/progress.cpp \
           widgets/scrollbar.cpp \
           widgets/scrollpane.cpp \
           widgets/sidebar.cpp \
           widgets/slider.cpp \
           widgets/textbox.cpp \
           widgets/toolbar.cpp \
           widgets/view.cpp \
           widgets/window.cpp \
           widgets/winstyle.cpp \
           widgets/knotwindow.cpp \
		   widgets/layout/border_layout.cpp \
		   widgets/layout/box_layout.cpp

S_SRCS = _fastcpy_arm64.s

CPP_OBJS = $(patsubst %.cpp,$(OBJDIR)/%.o,$(CPP_SRCS))
S_OBJS = $(patsubst %.s,$(OBJDIR)/%.o,$(S_SRCS))

OBJS = $(CPP_OBJS) $(S_OBJS)

all: $(TARGET)

$(DEFFILE): $(OBJS)
	@echo "Generating export list..."
	@echo "EXPORTS" > $@
	for obj in $(OBJS); do \
		llvm-nm --defined-only -g $$obj | awk '{print $$3}'; \
	done | sort -u >> $@

$(DLLTARGET): $(OBJS) $(DEFFILE)
	@echo "Linking $(DLLTARGET)..."
	$(CXX) $(CXXFLAGS) -fuse-ld=lld-link -nostdlib -shared -o $(DLLTARGET) $(OBJS) $(LIBS) \
	-Wl,/DEF:$(DEFFILE) -Wl,/IMPLIB:$(IMPLIB) -Wl,-noentry -Wl,-nodefaultlib -Wl,-filealign:0x1000 \
	-Wl,-base:0x0000000400000000

$(TARGET): $(OBJS)
	@echo "Archiving $(TARGET)..."
	$(AR) rcs $@ $(OBJS)

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(CC) $(CXXFLAGS) -Wno-unused-command-line-argument -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
