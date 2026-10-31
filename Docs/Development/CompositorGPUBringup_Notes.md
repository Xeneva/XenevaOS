# getting virtio-gpu to actually work

so today's goal started simple: add partial redraws to the compositor,
maybe get zero-copy framebuffers going through virtio-gpu. turns out both
of those were basically already built and just not wired together. the
real story of today was everything virtio-gpu touched underneath that
falling apart the second I tried to actually look at it, because nobody
had ever gotten a picture on screen through it before.

## the boot stack was never real

first thing, before any of the GPU stuff — the plain (non-`--bleed`) boot
path just crashed. always. on pristine master. turns out `xnldr.cpp` maps
a dedicated 1MB stack for the kernel before jumping in, and the switch
code in `callKernel` that's supposed to point `sp` at it was just...
commented out. the kernel had been running the entire time on whatever
stack UEFI happened to leave lying around. worked fine until something
needed real stack depth (`boot_self_test`'s big locals), and `--bleed`
skips that test, which is exactly why nobody noticed. bisected it with
git stash against master to prove it wasn't me, then GDB + disassembly
to find the actual faulting instruction. one-line fix, `add x1,x1,x2 /
mov sp,x1`, should've always been there.

## the gpu commands that never finished

this was the deep one. virtio-gpu commands were "completing" via a fixed
busy-spin that didn't actually check anything — the `switch` inside the
loop had a `break` that only escaped the switch, not the loop, so every
command just waited out a timer regardless of what the device did. fixed
that to wait on the IRQ flag properly instead, like the tablet driver
already does.

except then it turned out the IRQ handler was reading the DMA response
without invalidating cache first — same class of bug as the tablet ring
reads I fixed earlier this session, just missed here. fixed that with
`dc_ivac()`... except `dc_ivac()` wasn't actually linkable into a driver
DLL. it's in the kernel's export list on paper, but the header never had
`AU_EXPORT` on it, so the generated `.lib` had no import stub, and
`lld-link` was silently eating the unresolved symbol thanks to
`-force:unresolved`. the call was a null jump at runtime the entire time.
added the export, rebuilt the kernel, fixed.

then `_resp_ok` — the flag the IRQ wait loop checks — turned out to be
plain `bool`, not `volatile`. and here's the thing that actually explains
why NONE of this was ever caught before: at `-O2`, clang can just delete
a busy-wait loop that has no observable side effects, or hoist the load
out of it. the OLD, broken code silently had this happen, which is
*why it never hung* — the compiler threw away the dead check. the moment
I made it volatile to fix it properly, it became a real loop, which
finally exposed what was actually going on underneath: the interrupt
straight up never reaches the CPU. confirmed by sampling the stuck
program counter with GDB — it was sitting inside the idle loop's `_wfi()`,
genuinely parked waiting for an interrupt that structurally can't arrive.
never fully chased why (every driver here has `GICSetTargetCPU()`
commented out, so it's not obviously that).

the actual fix that shipped: stopped depending on the interrupt
entirely. poll the response buffer directly instead — invalidate, check
if the type field is nonzero, repeat. device answers fast in practice
(qemu's software virtio-gpu backend basically processes synchronously),
so this turned out way faster than waiting on a broken IRQ ever would've
been anyway. fps went from 14 to ~66 the moment this landed.

debugging trick worth remembering: spun up an isolated qemu instance
with `-s -S` + qmp over a unix socket, scripted the boot-menu keystrokes
through qmp `send-key`, then let gdb `continue` and interrupt it whenever
to sample state — reproduced the hang and found the root cause without
ever touching the window the user was actually looking at.

## other stuff that fell out along the way

- **fake scatter-gather backing**: `virt_gpu_alloc_fb` allocated pages
  one at a time but told the device `ATTACH_BACKING` with one entry
  spanning the whole thing, claiming contiguity it never actually
  requested. only worked because early boot allocation happens to be
  contiguous. now it actually allocates contiguous pages.
- **cursor leaving a ghost over menus**: restore was blending instead of
  copying. `CursorStoreBack` saves with a plain copy, `CursorDrawBack`
  was restoring with an alpha blend — over anything translucent that
  leaves the old cursor pixels bleeding through. also found an
  out-of-bounds write in the same function (bound check compared against
  the wrong axis, `canvasWidth` instead of `canvasHeight`).
- **netmngr took the whole kernel down**: `AuRawSocketSend` calls
  `device->write(...)` on whatever network adapter it's bound to.
  virtio-net's adapter node never populates a `.write` callback — tx
  goes through a different path — so this was a null function pointer
  call, straight up crashing the entire boot the second netmngr tried to
  send anything. guarded it. netmngr still doesn't actually work over
  virtio-net (that needs real tx wiring, didn't touch it), but at least
  it doesn't nuke the desktop anymore. also: netmngr never had a linux
  build at all, just a `.vcxproj`. gave it a Makefile and wired it into
  both build scripts.
- **the go-button hide-loop**: clicking the taskbar's go button was
  spamming hide-toggle messages. added a 250ms cooldown on the actual
  toggle action since the debounce logic itself already looked correct
  and I couldn't fully pin down why it was still firing repeatedly.

  then immediately regressed it myself — put the repaint call *inside*
  the cooldown gate along with the action, so a click that got
  suppressed by the cooldown still left the button (and anything else
  sharing that handler, i.e. the taskbar icons) permanently stuck
  showing whatever it looked like before the click. looked exactly like
  "the icons disappeared." moved the repaint back outside the gate, only
  the actual action stays cooldown-gated.

  found a second bug in the same function while I was in there:
  releasing the mouse button while still hovering never repainted back
  to normal — the reset branch only fires when you're *not* hovering
  anymore, and normally you don't move the mouse away right after
  releasing it. so buttons stayed showing their "pressed" artwork
  forever after a click. same exact pattern in XELnch's launch buttons,
  fixed both.

## what's still broken

**green corruption** on the launcher window — a rectangular patch renders
tinted green, wallpaper still visible through it so it's a blend result,
not garbage memory. position moves around between boots. tried forcing
an unconditional full-screen gpu resync alongside the new per-rect one as
a diagnostic: it reduced the corruption but didn't kill it, which means
it's *two* bugs — some of it really is a missed damage-rect (a region
gets painted correctly into the shared buffer but never gets flagged
dirty, so the gpu-side resource never resyncs), and some of it is
actually wrong pixel data somewhere, most likely in the glass-blur blend
path since the launcher uses `WINDOW_FLAG_GLASS`. tried disabling glass
on the launcher to isolate it, but that test run got derailed by the
netmngr crash before I got a clean read — need to rerun that cleanly.
if I were picking this back up, I'd look hard at the occlusion-clipping
path that splits a window's composite into sub-rects when two
always-on-top windows overlap (namdapha's taskbar and the launcher both
qualify) — a bug in how the blur buffer gets sliced per sub-rect would
produce exactly this kind of clean rectangular artifact.

**duplicate icon at the taskbar edges** — spotted once, didn't get to
dig in. one loose thread: the clock button's x-position reuses
`NAMDAPHA_WIDTH` (the taskbar's own thickness constant) as if it were the
clock button's own width, which smells wrong but isn't confirmed as the
cause.

**the hide-loop might still be back** — saw a burst of ~7 toggles again
after the fixes above landed. couldn't tell from the log alone whether
that's a real residual bug or just me clicking go repeatedly to test the
fix — they look identical from the log. would need a print at the actual
send site with calling context to tell them apart.

**windows key blanking the menu** — reported once, focus was genuinely on
qemu so it's not a host-intercept thing. best guess, unverified: XELnch's
key handler has no allowlist for what counts as valid search-box input —
any key that isn't backspace and isn't caught by the control-key check
gets typed into the search bar, which then filters the app grid down to
whatever matches. if the windows key's scancode doesn't map to a real
control key, it'd silently type garbage into the search box and filter
the grid to zero results — which looks exactly like the menu going blank.
was mid-way through checking what the windows key actually maps to in
the scancode table when this got interrupted.

## random ops notes so I don't relearn these

- `pkill -f "<pattern>"` will happily kill the shell that's currently
  running the pkill command itself, if the pattern shows up in that
  command's own text (it does, since you just typed it). did this twice
  tonight. use `ps aux | awk` to grab exact pids and `kill` them directly
  instead.
- fat.img "failed to get write lock" just means a previous qemu hasn't
  let go of it yet — check `ps aux | grep qemu-system` before relaunching.
- spinning up a second, isolated qemu instance (copy of fat.img, short
  socket paths, `-s -S`) is the move for debugging a hang live without
  disturbing whatever's already open in front of someone.

--axiss
