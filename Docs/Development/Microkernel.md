# Component model and user-space drivers

How far Aurora (AArch64) is from a microkernel, what runs where, and how to
move the next driver out. Everything here is on branch `component-model` and
was verified on QEMU `virt` (see "Testing").

## Layers

1. **Loadable components (Phase 1).** Drivers are PE `.dll` components with a
   lifecycle: `AuDriverMain` -> running -> `AuDriverUnload`. The kernel ledgers
   what a component acquires (IRQs, pages, MMIO) and frees it on unload.
   Opt in with `AU_DRIVER_FLAGS(AU_DRV_FLAG_UNLOADABLE)`. `/dev/drvctl`
   (`load|unload|list NAME`, daemons group only) drives it. Built-in drivers
   (`virtio-kbd`, `virtio-tablet`) use the same verbs through
   `KernelAA64/Drivers/builtin.c`.
2. **User-space drivers (Phase 2, MK1-MK4).** A server in the
   `MISC_DAEMONS` group owns a PCI function through capabilities:
   `/dev/hwdev` gives config space, BAR mappings, contiguous DMA, and an MSI-X
   interrupt it can wait on. Access is default-deny (allowlist in
   `Fs/Dev/devhw.c`: virtio-snd, virtio-input, virtio-net). The kernel keeps a
   thin *proxy* so the rest of the system is unchanged:

   | server (`Process/`) | proxy node | kernel proxy | replaces |
   |---|---|---|---|
| `sndsrv` | `/dev/sndsrv` | `Sound/sndproxy.c` | `virtsnd.dll` |
| `inputsrv` | `/dev/inputsrv` | `Fs/Dev/devinputsrv.c` | `virtiokbd.c`, `virtiotablet.c` |
| `netsrv` | `/dev/netsrv` | `Net/netproxy.c` | `virtnet.dll` |
| `gpusrv` | `/dev/gpusrv` | `Fs/Dev/devgpu.c` | `virtgpu.dll` |

3. **Supervisor (MK2).** `svcmgr` starts the servers from `/services.cnf`,
    restarts them with backoff, and after repeated crashes hands the device back
    to the in-kernel driver (`drvctl load ...`). Verified: crash -> restart ->
    service resumes. Crash-loop fallback now has signed handoff status and
    reset/resource cleanup, but live kernel-driver recovery remains an open
    acceptance gate.
4. **Generic IPC and user-space file systems (MK4d/e).** `/dev/ipc`
   (`BaseHdr/auipc.h`, `KernelAA64/Ipc/auipc.c`) gives named endpoints with
   synchronous call/reply. A daemons-group server creates an endpoint; any
   process its ACL admits calls it by name. The kernel copies messages, tells the
   server the caller's real pid/uid/gid, fails waiting callers if the server dies
   and drops late replies after a timeout. The kernel can be a caller too
   (`AuIpcKCall*`), which is how `/srv` works: `KernelAA64/Fs/srvfs.c` mounts a
   file system whose every open/read/write is forwarded to the `fs` endpoint,
   served by `Process/fssrv` (a tmpfs; protocol in `BaseHdr/fssrv.h`). The
   server enforces permissions from the forwarded uid, and `svcmgr` restarts
   it if it dies.

   A sleeping syscall resumes after the `svc`, it does not return up the C
   stack, so a proxy operation that must wait uses **restartable syscalls**: it
   queues the request, rewinds `ELR_EL1` by one instruction and sleeps; the
   syscall re-executes from its entry and each proxy operation is matched, in
   order, against a per-thread history (finished ones replay their saved answer,
   file positions are restored first). One syscall can span several server round
   trips (a 10000-byte write is three). Requests the server does not answer in
   3 s fail, so a dead server never hangs a client.
5. **Unikernel profile (Phase 3).** `--static-components` links the virtio
   drivers into the kernel (`KernelAA64/staticcomp.c`) with no `.dll` files.
   See `__XENEVA_STATIC_COMPONENTS__` in PreprocessorMacros.md. Mutually
   exclusive in practice with the user-space servers (static components cannot
   be unloaded).
6. **Raw block IPC groundwork (MK5a).** `BLKSRV=1` leaves the boot disk,
   persistent `data.img`, and FAT/root path in the kernel, while a fresh third
   QEMU disk is intentionally left unbound for `blkserv`. The server owns that
   virtio-blk function through `/dev/hwdev` and exposes the `blk` endpoint over
   generic IPC. `BLKTEST=1` runs ordinary and daemon clients through bounded
   reads/writes, protocol rejection, and supervised restart checks. This is
   deliberately not root-fs migration yet.

## The proxy pattern (adding the next server)

A proxy is a device node plus a shared ring in a DMA buffer **owned by the
server** (`AuHwdevOwnsDma` checks it). The kernel side:

* never waits on the server (full ring => drop, count it);
* length-checks and copies out anything it parses from the ring;
* detaches *before* the ring is freed (`AuHwdevCapReleased` -> `...OwnerGone`);
* keeps persistent state (e.g. `AuNetworkDevice` IP config, adapter nodes)
  across a server restart so open handles never dangle.

The server side: unload the in-kernel driver, claim the device, bring up
virtio (see `Process/netsrv/main.cpp`, the shortest complete example), attach
the ring, loop; on any setup failure `drvctl load` the kernel driver back.

## Still in the kernel (not done)

* the boot filesystem (FAT, devfs, VFS) and the block driver it boots from --
  `/srv` shows the mechanism but the root filesystem was not moved;
  `BLKSRV=1` only exposes a disposable scratch disk through IPC as MK5a
  groundwork;
* the TCP/IP stack, sockets, routing (only the NIC driver moved);
* the syscall surface (`Serv/*`) itself; only `/srv` is routed over IPC;
* `/srv` limits: flat namespace (one directory), no rename or mkdir, at most one
  page per read/write operation; `RemoveFile` is implemented for `/srv` only;
  file contents live in `fssrv`'s memory and are lost if it restarts.
* Not done on real hardware: DMA is mapped cacheable (needs uncached mappings
  or cache maintenance); x64 is not ported.

## Roadmap (MK5, not started; deliberately outside the first checklist)

Each item is its own multi-stage project:

1. **Boot file system.** A root-fs server needs a bootstrap story: a minimal
   initrd loader in the kernel starts it before `init`, and the block driver
   (virtio-blk, the `disk1`/`disk2` data disks first) moves with it. The restartable
   syscall engine in `srvfs.c` handles one wait per operation but the FAT code makes
   many dependent reads per syscall, so a block proxy needs its own batching.
2. **TCP/IP stack.** A socket protocol over IPC plus shared rings for payload;
   UDP/ICMP first, TCP last. The in-kernel console and DHCP/NTP clients are
   consumers to migrate with it.
3. **GPU driver.** **In progress for the QEMU virtio-GPU 2D path:** `gpusrv` owns the
   PCI function through `/dev/hwdev`; `/dev/gpusrv` keeps the kernel-owned
   `/dev/graph` staging framebuffer stable and copies bounded damage into
   server-owned DMA backing. Zero-copy pinned graphics buffers remain future
   optimization work. Proxy boundary regressions run with
   `bash Tests/test_gpuproxy.sh`: authoritative framebuffer geometry, padded
   pitch and byte extents, page-table-aware ioctl copying, request snapshots,
   and failure-atomic ACQUIRE/TAKE replies. User-page checks and publication
   are serialized by IRQ exclusion on the current single-CPU path; this is
   not SMP page pinning or synchronous bus-fault recovery. Full-frame copying
    remains IRQ-masked and needs latency measurement. Crash/restart with actual
    repaint is verified by `Tests/test_gpurestart.sh`; kernel-driver fallback
    remains a separate acceptance gate because a supervisor handback result or
    "BACK" message alone does not prove presentation recovered.
4. **Syscall layer.** Generalise `/srv`: let any `/dev` node be served over IPC.
   Kernel-internal consumers (console paste from the clipboard, TCP initial
   sequence numbers from `/dev/random`) need a kernel-side cache or a non-blocking
   fallback first.

## Known limits

* Unloading `virtnet.dll` leaks about 132 pages (queue memory from a kernel
  helper the ledger does not see).
* Server wake-up latency is up to one scheduler tick.
* One IRQ per hwdev handle.

## Demo: kill a driver, watch it come back

```
NETSRV=1 SVCTEST=netrestart QEMU_TIMEOUT=150 \
  Scripts/Linux/build_and_run_qemu.sh --llvm --bleed --headless | grep -a -E '>>>|nettest|svcmgr'
```

`netsrv` (the virtio-net driver, a user-space process) crashes on purpose after a
few seconds while `nettest` keeps pinging the gateway. `svcmgr` prints what it is
doing, each stage marked `>>>`:

```
[svcmgr] netsrv (pid 7) is gone after 18643 ms
[svcmgr] >>> netsrv is DOWN, the rest of the system keeps running; supervisor is recovering it
[svcmgr] >>> netsrv will RESTART automatically in 200 ms (attempt 1 of 3)
[svcmgr] started netsrv pid=13 (start #2)
[svcmgr] >>> netsrv is BACK (pid 13), service restored after 206 ms of downtime
```

Variants: `SVCTEST=netfallback` (crash loop, then the driver is handed back to the
in-kernel `virtnet.dll` and traffic still flows), `SVCTEST=restart|fallback` for
the sound server, `SRVTEST=1` for a user-space file system that survives its
server dying. The reported downtime runs from noticing the death to launching the
new process; it does not include the new server's own device setup.

### GPU server restart proof

The GPU restart path has a dedicated end-to-end assertion. It waits for a real
GPU frame to be transferred, flushes and ACKed, crashes `gpusrv`, then requires
the supervisor to start it again and the proxy to publish a new generation and
present another frame:

```
Tests/test_gpurestart.sh
```

The script accepts the bounded QEMU timeout (`124`) but fails unless it sees
`acquired generation=1`, a generation-1 presentation, the intentional crash,
the supervisor DOWN/RESTART/BACK sequence, `acquired generation=2`, and a
generation-2 presentation. This proves server restart and repaint from the
stable compositor staging framebuffer; it does not claim that the live
compositor can switch to a newly allocated kernel fallback framebuffer.

## Testing

`Scripts/Linux/build_and_run_qemu.sh --llvm --bleed --headless` with:

* `SNDSRV=1` / `INPUTSRV=1` / `NETSRV=1` -- build and pack that server (with `svcmgr`);
* `SVCTEST=restart|fallback|recover|selfcrash|inrestart|infallback|netrestart|netfallback|gpurestart|gpufallback`
  -- build the crash hooks and run that supervisor scenario;
* `NETTEST=1` -- pack `nettest` (gateway ping + sleep-accuracy probe) alone;
* `IPCTEST=1` -- `ipcsrv`+`ipctest`, 19 checks of the generic IPC;
* `SRVTEST=1` -- `fssrv` (under `svcmgr`) + `srvtest` run as two users, 32 checks of `/srv`: data, permissions, listing, removal, server crash and supervised recovery;
* `BLKSRV=1 BLKTEST=1` -- build `blkserv`, leave the third scratch disk
  unbound, claim it from user space, and run the `blk` IPC client suite (the
  boot and persistent data disks remain kernel-owned);
* `GPUSRV=1` -- build and supervise `gpusrv`, present the stable `/dev/graph`
  framebuffer through user-space virtio-GPU commands, and retain `virtgpu` as
  the supervisor hand-back fallback;
* `HWTEST=1` -- the hwdev access test;
* `--static-components` -- the unikernel profile.
