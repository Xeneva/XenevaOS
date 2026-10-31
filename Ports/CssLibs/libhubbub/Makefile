#!/bin/make
#
# Makefile for libhubbub
#
# Copyright 2009-2020 John-Mark Bell <jmb@netsurf-browser.org>

# Component settings
COMPONENT := hubbub
COMPONENT_VERSION := 0.3.8
# Default to a static library
COMPONENT_TYPE ?= lib-static

# Setup the tooling
PREFIX ?= /opt/netsurf
NSSHARED ?= $(PREFIX)/share/netsurf-buildsystem
include $(NSSHARED)/makefiles/Makefile.tools

TESTRUNNER := $(PERL) $(NSTESTTOOLS)/testrunner.pl

# Toolchain flags
WARNFLAGS := -Wall -W -Wundef -Wpointer-arith -Wcast-align \
	-Wwrite-strings -Wstrict-prototypes -Wmissing-prototypes \
	-Wmissing-declarations -Wnested-externs -pedantic
# BeOS/Haiku/AmigaOS have standard library errors that issue warnings.
ifneq ($(BUILD),i586-pc-haiku)
  ifneq ($(findstring amigaos,$(BUILD)),amigaos)
    WARNFLAGS := $(WARNFLAGS) -Werror
  endif
endif

CFLAGS := -D_BSD_SOURCE -D_DEFAULT_SOURCE \
	 -I$(CURDIR)/include/ -I$(CURDIR)/src \
	$(WARNFLAGS) $(CFLAGS)
ifneq ($(GCCVER),2)
  CFLAGS := $(CFLAGS) -std=c99
else
  # __inline__ is a GCCism
  CFLAGS := $(CFLAGS) -Dinline="__inline__"
endif

# Parserutils
ifneq ($(findstring clean,$(MAKECMDGOALS)),clean)
  ifneq ($(PKGCONFIG),)
    CFLAGS := $(CFLAGS) $(shell $(PKGCONFIG) libparserutils --cflags)
    LDFLAGS := $(LDFLAGS) $(shell $(PKGCONFIG) libparserutils --libs)
  else
    CFLAGS := $(CFLAGS) -I$(PREFIX)/include
    LDFLAGS := $(LDFLAGS) -lparserutils
  endif
endif

include $(NSBUILD)/Makefile.top

ifeq ($(WANT_TEST),yes)
  # We require the presence of libjson -- http://oss.metaparadigm.com/json-c/
  ifneq ($(PKGCONFIG),)
    TESTCFLAGS := $(TESTCFLAGS) \
		$(shell $(PKGCONFIG) $(PKGCONFIGFLAGS) --cflags json-c)
    TESTLDFLAGS := $(TESTLDFLAGS) \
		$(shell $(PKGCONFIG) $(PKGCONFIGFLAGS) --libs json-c)
  else
    TESTCFLAGS := $(TESTCFLAGS) -I$(PREFIX)/include/json-c
    TESTLDFLAGS := $(TESTLDFLAGS) -ljson-c
  endif

  ifneq ($(GCCVER),2)
    TESTCFLAGS := $(TESTCFLAGS) -Wno-unused-parameter
  endif
endif

# Extra installation rules
I := /$(INCLUDEDIR)/hubbub
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/hubbub/errors.h
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/hubbub/functypes.h
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/hubbub/hubbub.h
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/hubbub/parser.h
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/hubbub/tree.h
INSTALL_ITEMS := $(INSTALL_ITEMS) $(I):include/hubbub/types.h
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(LIBDIR)/pkgconfig:lib$(COMPONENT).pc.in
INSTALL_ITEMS := $(INSTALL_ITEMS) /$(LIBDIR):$(OUTPUT)
