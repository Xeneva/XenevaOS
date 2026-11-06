# XenevaOS XR demo convenience targets (host-side only).
#
#   make xr-demo    Build the host XR tools: xeneva-xr-view (HMD viewer)
#                   and xeneva-xr-client (interactive monitor client).
#
# The guest side (DeodhaiXR with the OpenXR QEMU runtime) is configured and
# launched from the modular Bash TUI with:
#   Scripts/Linux/build_and_run_qemu.sh --llvm --xr-demo
#
# Full flow: build.sh --xr-demo  ->  viewer --egl  ->  Quest 2 (WiVRn).

.PHONY: xr-demo xr-view xr-client clean

xr-demo: xr-view xr-client

xr-view:
	$(MAKE) -C Tools/xeneva-xr-view

xr-client:
	$(MAKE) -C Tools/xeneva-xr-client

clean:
	$(MAKE) -C Tools/xeneva-xr-view clean
	$(MAKE) -C Tools/xeneva-xr-client clean
