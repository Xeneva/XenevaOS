# Host-only OpenXR quad viewer. Not built into the guest image.
CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17
CPPFLAGS += $(shell pkg-config --cflags openxr x11 gl egl gbm glesv2 gio-2.0)
LIBS += $(shell pkg-config --libs openxr x11 gl egl gbm glesv2 gio-2.0) -lXext

.PHONY: all check clean

all: xeneva-xr-view

xeneva-xr-view: xeneva-xr-view.cpp qemu_egl_capture.cpp qemu_egl_capture.h pointer_filter.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ xeneva-xr-view.cpp qemu_egl_capture.cpp $(LIBS)

pointer_filter_test: pointer_filter_test.cpp pointer_filter.h
	$(CXX) $(CXXFLAGS) -o $@ pointer_filter_test.cpp

check: pointer_filter_test
	./pointer_filter_test

clean:
	rm -f xeneva-xr-view pointer_filter_test
