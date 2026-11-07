# AArch64 application memory reclamation

## Exit and reaping

`AuProcessExit` stops the process's other threads, detaches shared memory,
closes descriptors and wakes all waiters. It publishes `PROCESS_STATE_DIED`
after these steps. The exiting thread finishes its syscall/abort handoff;
the reaper only selects a process once all of its threads are killable and
its address space is inactive. Both reaper selection and cleanup use
`AuProcessCanReap` for this precondition; waiter-list ownership is handled
by `AuProcessWakeWaiters`.

`AuProcessClean` removes thread objects and kernel-stack mappings, then
destroys the process's private lower-half page tables. Walking the actual
page tables covers executable images, signal trampolines, environment/argv
pages, user stacks, heap pages and sparse or explicitly addressed mmap
regions. It also frees VMA list entries and the process's root table.
Inherited upper-half kernel tables, device mappings and file-cache frames
remain owned by their respective subsystems. SHM frames remain live until
the segment's last mapping and kernel ownership reference are released.
The font registry retains each segment independently of client mappings;
otherwise closing the last Consolas user leaves the registry pointing into
freed memory and the next Terminal cannot load its font. `AuSHMRetain` and
`AuSHMRelease` make that cache ownership explicit.
Gap reuse and new SHM allocations share one mapping path, and individual/bulk
detaches share one release path.

Unmapping clears and publishes PTEs and invalidates translations before
releasing physical frames. Lookup-only page-table queries do not allocate
tables for missing mappings.

## Why poisoning exposed the problem

The old cleaner freed the process through `AuRemoveProcess`, then passed
its PID and name to `AuPmmOwnerTeardownCheck`. Free poisoning overwrote
those fields with `0x6B`, so diagnostics checked a bogus owner instead of
the dead process. The process must remain live until that check finishes.
Descriptor cleanup also read file flags after freeing the file object.

There were independent reclamation gaps: kernel-loaded executable pages,
signal trampolines, page tables and VMA records were omitted, range-counter
cleanup missed sparse mappings, and one SHM gap-reuse path retained an
extra physical-page reference without releasing it on detach.

## Verification

Run the host regression from the repository root:

```sh
bash Tests/test_process_teardown.sh
```

It compiles the real process, cleaner, VMM, SHM, font manager and list
implementations, with simulated hardware/PMM hooks, ASan/UBSan and fresh/free
heap poisoning.
It checks exact heap/frame restoration over 20 repeated lifecycles, sparse
and non-executable mappings, cached/device and inherited kernel mappings,
kernel-stack VA reuse, thread aliases, all waiters, pending argv copies,
partially created slots, batch reaping, empty/interior SHM gap reuse, and
detaching SHM from a process other than the currently active address space.
It also maps and tears down three consecutive users of the same cached font,
checks that its ID, data and frames survive, and verifies final reclamation
when the font registry releases its ownership reference.

For a guest run with the allocator detectors enabled:

```sh
Scripts/Linux/build_and_run_qemu.sh --llvm --debug-alloc --headless
```

After repeatedly opening and closing the same app, its owner check should
report its real name/PID and `teardown clean (0 pages)`. Kernel TLSF pools
and file caches retain backing pages for reuse, so global used RAM may
settle above the initial boot baseline. Repeated identical app cycles
should stabilize rather than accumulate private pages.
