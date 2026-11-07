# Interactive QEMU monitor client for the XR demo. No guest code.
CXX ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++17

.PHONY: all clean

all: xeneva-xr-client

xeneva-xr-client: xeneva-xr-client.cpp
	$(CXX) $(CXXFLAGS) -o $@ xeneva-xr-client.cpp

clean:
	rm -f xeneva-xr-client
