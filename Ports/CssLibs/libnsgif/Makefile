#!/bin/make
#
# Makefile for libnsgif
#
# Copyright 2009-2015 John-Mark Bell <jmb@netsurf-browser.org>

# Component settings
COMPONENT := nsgif
COMPONENT_VERSION := 1.0.0
# Default to a static library
COMPONENT_TYPE ?= lib-static

# Setup the tooling
PREFIX ?= /opt/netsurf
NSSHARED ?= $(PREFIX)/share/netsurf-buildsystem
include $(NSSHARED)/makefiles/Makefile.tools

# Reevaluate when used, as BUILDDIR won't be defined yet
TESTRUNNER = test/runtest.sh $(BUILDDIR) $(EXEEXT)

# Toolchain flags
WARNFLAGS := -Wall -Wextra -W -Wundef -Wpointer-arith -Wcast-align \
	-Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations -Wnested-externs -pedantic
# BeOS/Haiku standard library headers create warnings
ifneq ($(BUILD),i586-pc-haiku)
  WARNFLAGS := $(WARNFLAGS) -Werror
endif

CFLAGS := -DNSGIF_NAME=$(COMPONENT) \
	-DNSGIF_VERSION=$(COMPONENT_VERSION) \
	-I$(CURDIR)/include/ -I$(CURDIR)/src \
	$(WARNFLAGS) $(CFLAGS)
ifneq ($(GCCVER),2)
  CFLAGS := $(CFLAGS) -std=c99
else
  # __inline__ is a GCCism
  CFLAGS := $(CFLAGS) -Dinline="__inline__"
endif

TESTCFLAGS := -g -O2
TESTLDFLAGS := -lm -l$(COMPONENT) $(TESTLDFLAGS)

include $(NSBUILD)/Makefile.top

# Extra installation rules
I := /$(INCLUDEDIR)
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/nsgif.h
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(LIBDIR)/pkgconfig:lib$(COMPONENT).pc.in
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(LIBDIR):$(OUTPUT)
