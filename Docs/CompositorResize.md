# Compositor resize stalls

The apparent crash during GTK testing was a userspace compositor stall. Serial
output continued from the scheduler and network daemon while desktop rendering
stopped. The resize path waited for 10,000 sleeps of 10 ms, then logged
`failed to reinitialize buffer for : Xeneva Terminal`. Queued mouse events could
trigger more waits, producing frame times of 80–190 seconds and input drops.

Terminal has a fixed cell grid and a separate painting thread, but no handlers for
the destroy/reinitialize-buffer protocol. It now advertises
`WINDOW_FLAG_NON_RESIZABLE`; moving and maximizing the window remain available.

## Resize lifetime

- Drag previews keep their geometry private to the compositor. Shared dimensions
  retain the old buffer's stride until the client acknowledges destruction.
- A resize request returns immediately. Replies are handled by the normal frame
  loop, so unrelated create, close, keyboard, and mouse events remain serviceable.
- Replies must match the owner, window handle, and old buffer key. An unrelated
  or stale acknowledgement cannot replace another window's mapping.
- Only after acknowledgement does the compositor replace the buffer, update its
  key and dimensions, and send the new key to the client. Replacement buffers
  start zeroed; glass/shadow storage is resized to match.
- Closing a window releases its compositor-owned glass/shadow storage as well
  as its shared buffers, including each popup's own effect allocations.
- After one second without a reply, further resizing is disabled for that window.
  Its original buffer remains valid. A late matching reply is still accepted so
  a client that eventually detached can reattach safely.
- Allocation failure sends the retained old key back with the old dimensions.

The Calculator client uses the shared Chitralekha acknowledgement helper so its
reply carries the owner, handle, and buffer key.

## Verification

```sh
bash Tests/test_deodhai_resize.sh
Scripts/Linux/build_and_run_qemu.sh --llvm --force-user-apps
```

The host regression uses ASan/UBSan and rejects any nested postbox event read.
It covers silent clients, independent pending resizes, unrelated/stale replies,
timeouts, late replies, unchanged mapping geometry while pending, and allocation
recovery. In GTK, attempt to resize Terminal, resize Calculator, and resize a
client without resize handlers (such as Controls). Rendering and input should
continue, and windows should still close normally.
