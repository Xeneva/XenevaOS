#![no_std]
#![no_main]

use core::arch::asm;
use core::panic::PanicInfo;

/// Witness for the musl Linux syscall path Servo needs.
/// This is not Servo. Each call uses the aarch64 Linux number in x8.

const ENOSYS: i64 = -38;
const ENOTTY: i64 = -25;
const EEXIST: i64 = -17;
const EAGAIN: i64 = -11;
const ECHILD: i64 = -10;
const ENETUNREACH: i64 = -114;

const CLONE_VM: u64 = 0x100;
const CLONE_FS: u64 = 0x200;
const CLONE_FILES: u64 = 0x400;
const CLONE_SIGHAND: u64 = 0x800;
const CLONE_THREAD: u64 = 0x10000;
const CLONE_SYSVSEM: u64 = 0x40000;
const CLONE_SETTLS: u64 = 0x80000;
const CLONE_PARENT_SETTID: u64 = 0x100000;
const CLONE_CHILD_CLEARTID: u64 = 0x200000;
const CLONE_CHILD_SETTID: u64 = 0x1000000;

static mut PASS: u32 = 0;
static mut TOTAL: u32 = 0;
static mut RAN: u32 = 0;
static mut PTID: u32 = 0;
static mut CTID: u32 = 0;
static mut TLS: [u64; 2] = [0; 2];

#[repr(align(16))]
struct Stack([u8; 16384]);
static mut CHILD_STACK: Stack = Stack([0; 16384]);

unsafe fn syscall(nr: u64, a0: u64, a1: u64, a2: u64, a3: u64, a4: u64, a5: u64) -> i64 {
    let ret: u64;
    asm!(
        "svc #0",
        in("x8") nr,
        inout("x0") a0 => ret,
        in("x1") a1,
        in("x2") a2,
        in("x3") a3,
        in("x4") a4,
        in("x5") a5,
        options(nostack)
    );
    ret as i64
}

fn write_all(bytes: &[u8]) {
    if bytes.is_empty() {
        return;
    }
    unsafe {
        syscall(64, 1, bytes.as_ptr() as u64, bytes.len() as u64, 0, 0, 0);
    }
}

fn put_u64(mut v: u64) {
    let mut buf = [0u8; 20];
    let mut i = 20;
    if v == 0 {
        write_all(b"0");
        return;
    }
    while v > 0 {
        i -= 1;
        buf[i] = b'0' + (v % 10) as u8;
        v /= 10;
    }
    write_all(&buf[i..]);
}

fn put_i64(v: i64) {
    if v < 0 {
        write_all(b"-");
        put_u64(v.wrapping_neg() as u64);
    } else {
        put_u64(v as u64);
    }
}

fn report(name: &str, pass: bool, rc: i64) {
    unsafe {
        TOTAL += 1;
        if pass {
            PASS += 1;
        }
    }
    write_all(name.as_bytes());
    if pass {
        write_all(b" ok\n");
    } else {
        write_all(b" fail ");
        put_i64(rc);
        write_all(b"\n");
    }
}

fn ok_ge0(name: &str, rc: i64) {
    report(name, rc >= 0, rc);
}

fn ok_eq(name: &str, rc: i64, expect: i64) {
    report(name, rc == expect, rc);
}

#[no_mangle]
pub extern "C" fn child_entry() -> ! {
    unsafe {
        core::ptr::write_volatile(&raw mut RAN, 1);
        let word = &raw const RAN as u64;
        syscall(98, word, 1, 1, 0, 0, 0);
        syscall(93, 0, 0, 0, 0, 0, 0);
        loop {}
    }
}

unsafe fn spawn() -> i64 {
    let top = (&raw mut CHILD_STACK as usize + 16384) & !15;
    let flags = CLONE_VM
        | CLONE_FS
        | CLONE_FILES
        | CLONE_SIGHAND
        | CLONE_THREAD
        | CLONE_SYSVSEM
        | CLONE_SETTLS
        | CLONE_PARENT_SETTID
        | CLONE_CHILD_CLEARTID
        | CLONE_CHILD_SETTID;
    let tid: u64;
    asm!(
        "svc #0",
        "cbz x0, 2f",
        "b 3f",
        "2:",
        "bl {child}",
        "b 2b",
        "3:",
        child = sym child_entry,
        in("x8") 220u64,
        inout("x0") flags => tid,
        in("x1") top,
        in("x2") &raw mut PTID,
        in("x3") &raw mut TLS,
        in("x4") &raw mut CTID,
        clobber_abi("C"),
    );
    tid as i64
}

#[repr(C)]
struct Iovec {
    base: u64,
    len: u64,
}

#[repr(C)]
struct MsgHdr {
    name: u64,
    namelen: u32,
    _pad: u32,
    iov: u64,
    iovlen: u64,
    control: u64,
    controllen: u64,
    flags: i32,
}

#[repr(C)]
struct SockAddrIn {
    family: u16,
    port: u16,
    addr: u32,
    zero: [u8; 8],
}

#[repr(C)]
struct PollFd {
    fd: i32,
    events: i16,
    revents: i16,
}

fn addr_loopback() -> SockAddrIn {
    SockAddrIn {
        family: 2,
        port: 0,
        addr: u32::from_be(0x7f000001),
        zero: [0; 8],
    }
}

#[no_mangle]
pub extern "C" fn _start() -> ! {
    write_all(b"xeneva rust syscalls\n");
    unsafe { run() }
    loop {}
}

unsafe fn run() {
    let mut buf = [0u8; 325];
    let mut small = [0u8; 64];
    let mut stat = [0u8; 256];
    let mut ts = [0i64; 2];

    let cwd = syscall(17, buf.as_mut_ptr() as u64, 64, 0, 0, 0, 0);
    report("getcwd", cwd == 2 && buf[0] == b'/', cwd);

    let efd = syscall(19, 0, 0, 0, 0, 0, 0);
    ok_ge0("eventfd2", efd);
    if efd >= 0 {
        let one: u64 = 1;
        let n = syscall(64, efd as u64, &one as *const u64 as u64, 8, 0, 0, 0);
        let mut got: u64 = 0;
        let r = syscall(63, efd as u64, &mut got as *mut u64 as u64, 8, 0, 0, 0);
        report("eventfd2 io", n == 8 && r == 8 && got == 1, r);
    }

    let file = b"/r.dat\0";
    let dir = b"/rdir\0";
    let exe = b"/proc/self/exe\0";
    let fd = syscall(56, -100i64 as u64, file.as_ptr() as u64, 2 | 64, 0o644, 0, 0);
    ok_ge0("openat", fd);

    let mk = syscall(34, -100i64 as u64, dir.as_ptr() as u64, 0o755, 0, 0, 0);
    report("mkdirat", mk == 0 || mk == EEXIST, mk);

    let acc = syscall(48, -100i64 as u64, file.as_ptr() as u64, 4, 0, 0, 0);
    ok_eq("faccessat", acc, 0);

    if fd >= 0 {
        let msg = b"hi\n";
        let n = syscall(64, fd as u64, msg.as_ptr() as u64, msg.len() as u64, 0, 0, 0);
        report("write", n == msg.len() as i64, n);
        let off = syscall(62, fd as u64, 0, 0, 0, 0, 0);
        ok_eq("lseek", off, 0);
        let r = syscall(63, fd as u64, small.as_mut_ptr() as u64, 3, 0, 0, 0);
        report("read", r == 3 && small[0] == b'h', r);

        let iov = Iovec {
            base: msg.as_ptr() as u64,
            len: msg.len() as u64,
        };
        let wv = syscall(66, fd as u64, &iov as *const Iovec as u64, 1, 0, 0, 0);
        report("writev", wv == msg.len() as i64, wv);
        let _ = syscall(62, fd as u64, 0, 0, 0, 0, 0);
        let iov_r = Iovec {
            base: small.as_mut_ptr() as u64,
            len: 3,
        };
        let rv = syscall(65, fd as u64, &iov_r as *const Iovec as u64, 1, 0, 0, 0);
        report("readv", rv == 3, rv);

        let st = syscall(80, fd as u64, stat.as_mut_ptr() as u64, 0, 0, 0, 0);
        ok_eq("fstat", st, 0);
        let fa = syscall(79, -100i64 as u64, file.as_ptr() as u64, stat.as_mut_ptr() as u64, 0, 0, 0);
        ok_eq("newfstatat", fa, 0);
        let sx = syscall(291, -100i64 as u64, file.as_ptr() as u64, 0, 0, stat.as_mut_ptr() as u64, 0);
        ok_eq("statx", sx, 0);

        let ep = syscall(20, 0, 0, 0, 0, 0, 0);
        ok_ge0("epoll_create1", ep);
        if ep >= 0 {
            let mut ev = [0u8; 12];
            ev[0] = 5;
            let ctl = syscall(21, ep as u64, 1, fd as u64, ev.as_mut_ptr() as u64, 0, 0);
            ok_eq("epoll_ctl", ctl, 0);
            let mut out = [0u8; 12];
            let nw = syscall(22, ep as u64, out.as_mut_ptr() as u64, 1, 0, 0, 0);
            report("epoll_pwait", nw >= 1, nw);
            let _ = syscall(57, ep as u64, 0, 0, 0, 0, 0);
        }

        let d1 = syscall(23, fd as u64, 0, 0, 0, 0, 0);
        ok_ge0("dup", d1);
        let d2 = syscall(24, fd as u64, 0, 0, 0, 0, 0);
        ok_ge0("dup3", d2);
        let d3 = syscall(25, fd as u64, 0, 0, 0, 0, 0);
        ok_ge0("fcntl", d3);
        if d1 >= 0 {
            let _ = syscall(57, d1 as u64, 0, 0, 0, 0, 0);
        }
        if d2 >= 0 {
            let _ = syscall(57, d2 as u64, 0, 0, 0, 0, 0);
        }
        if d3 >= 0 {
            let _ = syscall(57, d3 as u64, 0, 0, 0, 0, 0);
        }
        let cl = syscall(57, fd as u64, 0, 0, 0, 0, 0);
        ok_eq("close", cl, 0);
    }

    let io = syscall(29, 1, 0x5401, 0, 0, 0, 0);
    ok_eq("ioctl", io, ENOTTY);

    let mut pipes = [0i32; 2];
    let pp = syscall(59, pipes.as_mut_ptr() as u64, 0, 0, 0, 0, 0);
    report("pipe2", pp == 0 && pipes[0] >= 0 && pipes[1] >= 0, pp);
    if pipes[0] >= 0 {
        let _ = syscall(57, pipes[0] as u64, 0, 0, 0, 0, 0);
    }
    if pipes[1] >= 0 {
        let _ = syscall(57, pipes[1] as u64, 0, 0, 0, 0, 0);
    }

    let dfd = syscall(56, -100i64 as u64, dir.as_ptr() as u64, 0, 0, 0, 0);
    if dfd >= 0 {
        let gd = syscall(61, dfd as u64, buf.as_mut_ptr() as u64, 64, 0, 0, 0);
        ok_eq("getdents64", gd, 0);
        let _ = syscall(57, dfd as u64, 0, 0, 0, 0, 0);
    } else {
        report("getdents64", false, dfd);
    }

    let mut pfd = PollFd {
        fd: 1,
        events: 4,
        revents: 0,
    };
    let pol = syscall(73, &mut pfd as *mut PollFd as u64, 1, 0, 0, 0, 0);
    ok_ge0("ppoll", pol);

    let rl = syscall(78, -100i64 as u64, exe.as_ptr() as u64, small.as_mut_ptr() as u64, 64, 0, 0);
    report("readlinkat", rl == 10, rl);

    let tidptr = &raw mut CTID as u64;
    let tid = syscall(96, tidptr, 0, 0, 0, 0, 0);
    ok_ge0("set_tid_address", tid);
    let gt = syscall(178, 0, 0, 0, 0, 0, 0);
    report("gettid", gt == tid, gt);

    let mut word: u32 = 1;
    let wake = syscall(98, &mut word as *mut u32 as u64, 1, 1, 0, 0, 0);
    ok_ge0("futex", wake);
    word = 2;
    let again = syscall(98, &mut word as *mut u32 as u64, 0, 1, 0, 0, 0);
    ok_eq("futex wait", again, EAGAIN);

    ok_eq("set_robust_list", syscall(99, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("get_robust_list", syscall(100, 0, 0, 0, 0, 0, 0), 0);

    ts[0] = 0;
    ts[1] = 0;
    ok_eq("nanosleep", syscall(101, ts.as_ptr() as u64, 0, 0, 0, 0, 0), 0);
    ok_eq("clock_nanosleep", syscall(115, 0, 0, ts.as_ptr() as u64, 0, 0, 0), 0);
    ok_eq("clock_gettime", syscall(113, 0, ts.as_mut_ptr() as u64, 0, 0, 0, 0), 0);
    ok_eq("clock_getres", syscall(114, 0, ts.as_mut_ptr() as u64, 0, 0, 0, 0), 0);
    ok_eq("gettimeofday", syscall(169, ts.as_mut_ptr() as u64, 0, 0, 0, 0, 0), 0);

    ok_eq("sched_setscheduler", syscall(119, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("sched_getscheduler", syscall(120, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("sched_setaffinity", syscall(122, 0, 8, buf.as_mut_ptr() as u64, 0, 0, 0), 0);
    let aff = syscall(123, 0, 8, buf.as_mut_ptr() as u64, 0, 0, 0);
    report("sched_getaffinity", aff >= 8, aff);
    ok_eq("sched_yield", syscall(124, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("tkill", syscall(130, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("sigaltstack", syscall(132, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("rt_sigaction", syscall(134, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("rt_sigprocmask", syscall(135, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("rt_sigreturn", syscall(139, 0, 0, 0, 0, 0, 0), 0);

    let un = syscall(160, buf.as_mut_ptr() as u64, 0, 0, 0, 0, 0);
    report(
        "uname",
        un == 0 && buf[0] == b'L' && buf[260] == b'a',
        un,
    );

    ok_eq("getrlimit", syscall(163, 0, stat.as_mut_ptr() as u64, 0, 0, 0, 0), 0);
    ok_eq("prlimit64", syscall(261, 0, 0, 0, stat.as_mut_ptr() as u64, 0, 0), 0);

    let name = b"rust\0";
    ok_eq("prctl", syscall(167, 15, name.as_ptr() as u64, 0, 0, 0, 0), 0);
    small[..16].fill(0);
    let gn = syscall(167, 16, small.as_mut_ptr() as u64, 0, 0, 0, 0);
    report("prctl get", gn == 0 && small[0] == b'r', gn);

    ok_ge0("getpid", syscall(172, 0, 0, 0, 0, 0, 0));
    ok_ge0("getppid", syscall(173, 0, 0, 0, 0, 0, 0));
    ok_eq("getuid", syscall(174, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("geteuid", syscall(175, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("getgid", syscall(176, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("getegid", syscall(177, 0, 0, 0, 0, 0, 0), 0);
    ok_eq("sysinfo", syscall(179, buf.as_mut_ptr() as u64, 0, 0, 0, 0, 0), 0);

    let mut rnd = [0u8; 16];
    let gr = syscall(278, rnd.as_mut_ptr() as u64, 16, 0, 0, 0, 0);
    ok_eq("getrandom", gr, 16);

    let brk0 = syscall(214, 0, 0, 0, 0, 0, 0);
    let brk1 = syscall(214, (brk0 as u64).wrapping_add(0x1000), 0, 0, 0, 0, 0);
    report("brk", brk0 > 0 && brk1 >= brk0, brk1);

    let map = syscall(222, 0, 4096, 3, 0x22, -1i64 as u64, 0);
    report("mmap", map > 0, map);
    if map > 0 {
        core::ptr::write_volatile(map as *mut u8, 0x5a);
        ok_eq("mprotect", syscall(226, map as u64, 4096, 3, 0, 0, 0), 0);
        ok_eq("madvise", syscall(233, map as u64, 4096, 0, 0, 0, 0), 0);
        ok_eq("munmap", syscall(215, map as u64, 4096, 0, 0, 0, 0), 0);
    }
    let fixed = 0x2000_0000_0000u64;
    let mf = syscall(222, fixed, 4096, 3, 0x32, -1i64 as u64, 0);
    report("mmap fixed", mf == fixed as i64, mf);
    if mf == fixed as i64 {
        core::ptr::write_volatile(fixed as *mut u8, 1);
        let _ = syscall(215, fixed, 4096, 0, 0, 0, 0);
    }
    ok_eq("mremap", syscall(216, 0, 0, 0, 0, 0, 0), ENOSYS);
    ok_eq("membarrier", syscall(283, 0, 0, 0, 0, 0, 0), 0);

    let tcp = syscall(198, 2, 1, 0, 0, 0, 0);
    ok_ge0("socket", tcp);
    if tcp >= 0 {
        let mut sa = addr_loopback();
        let bd = syscall(200, tcp as u64, &mut sa as *mut SockAddrIn as u64, 16, 0, 0, 0);
        ok_eq("bind", bd, 0);
        ok_eq("listen", syscall(201, tcp as u64, 1, 0, 0, 0, 0), 0);
        ok_eq("shutdown", syscall(210, tcp as u64, 2, 0, 0, 0, 0), 0);
        let _ = syscall(57, tcp as u64, 0, 0, 0, 0, 0);
    }

    let udp = syscall(198, 2, 2, 0, 0, 0, 0);
    ok_ge0("socket dgram", udp);
    if udp >= 0 {
        let mut sa = addr_loopback();
        sa.port = 0x0900;
        let bd = syscall(200, udp as u64, &sa as *const SockAddrIn as u64, 16, 0, 0, 0);
        report("bind dgram", bd == 0 || bd == -1, bd);
        ok_eq("connect", syscall(203, udp as u64, &sa as *const SockAddrIn as u64, 16, 0, 0, 0), 0);
        let acc = syscall(202, udp as u64, 0, 0, 0, 0, 0);
        report("accept", acc != ENOSYS, acc);
        let acc4 = syscall(242, udp as u64, 0, 0, 0, 0, 0);
        report("accept4", acc4 != ENOSYS, acc4);
        let byte = b"z";
        let sent = syscall(206, udp as u64, byte.as_ptr() as u64, 1, 0, &sa as *const SockAddrIn as u64, 16);
        report("sendto", sent >= 0 || sent == ENETUNREACH, sent);
        let gotn = syscall(207, udp as u64, small.as_mut_ptr() as u64, 8, 0, 0, 0);
        ok_ge0("recvfrom", gotn);
        let iov = Iovec {
            base: byte.as_ptr() as u64,
            len: 1,
        };
        let msg = MsgHdr {
            name: &sa as *const SockAddrIn as u64,
            namelen: 16,
            _pad: 0,
            iov: &iov as *const Iovec as u64,
            iovlen: 1,
            control: 0,
            controllen: 0,
            flags: 0,
        };
        let sm = syscall(211, udp as u64, &msg as *const MsgHdr as u64, 0, 0, 0, 0);
        report("sendmsg", sm >= 0 || sm == ENETUNREACH, sm);
        let rm = syscall(212, udp as u64, &msg as *const MsgHdr as u64, 0, 0, 0, 0);
        ok_ge0("recvmsg", rm);
        let opt: u32 = 1;
        ok_eq(
            "setsockopt",
            syscall(208, udp as u64, 1, 2, &opt as *const u32 as u64, 4, 0),
            0,
        );
        let _ = syscall(57, udp as u64, 0, 0, 0, 0, 0);
    }

    ok_eq("wait4", syscall(260, -1i64 as u64, 0, 0, 0, 0, 0), ECHILD);
    ok_eq("rseq", syscall(293, 0, 0, 0, 0, 0, 0), ENOSYS);
    ok_eq("clone3", syscall(435, 0, 0, 0, 0, 0, 0), ENOSYS);

    core::ptr::write_volatile(&raw mut RAN, 0);
    core::ptr::write_volatile(&raw mut CTID, 0);
    let child = spawn();
    let stored = core::ptr::read_volatile(&raw const CTID);
    report("clone", child > 0 && stored == child as u32, child);
    let mut spins = 0;
    while core::ptr::read_volatile(&raw const CTID) != 0 && spins < 3 {
        ts[0] = 0;
        ts[1] = 200_000_000;
        let _ = syscall(98, &raw const CTID as u64, 0, stored as u64, ts.as_ptr() as u64, 0, 0);
        let _ = syscall(124, 0, 0, 0, 0, 0, 0);
        spins += 1;
    }
    let ran = core::ptr::read_volatile(&raw const RAN);
    let cleared = core::ptr::read_volatile(&raw const CTID);
    report("exit", ran == 1 && cleared == 0, ran as i64);

    let (pass, total) = (PASS, TOTAL);
    write_all(b"servo musl ");
    put_u64(pass as u64);
    write_all(b"/");
    put_u64(total as u64);
    write_all(b"\n");
    syscall(94, 0, 0, 0, 0, 0, 0);
}

#[panic_handler]
fn panic(_info: &PanicInfo) -> ! {
    loop {}
}

#[no_mangle]
pub unsafe extern "C" fn memset(s: *mut u8, c: i32, n: usize) -> *mut u8 {
    let mut i = 0;
    while i < n {
        core::ptr::write_volatile(s.add(i), c as u8);
        i += 1;
    }
    s
}

#[no_mangle]
pub unsafe extern "C" fn memcpy(d: *mut u8, s: *const u8, n: usize) -> *mut u8 {
    let mut i = 0;
    while i < n {
        core::ptr::write_volatile(d.add(i), core::ptr::read_volatile(s.add(i)));
        i += 1;
    }
    d
}

#[no_mangle]
pub unsafe extern "C" fn memmove(d: *mut u8, s: *const u8, n: usize) -> *mut u8 {
    if (d as usize) <= (s as usize) {
        memcpy(d, s, n)
    } else {
        let mut i = n;
        while i > 0 {
            i -= 1;
            core::ptr::write_volatile(d.add(i), core::ptr::read_volatile(s.add(i)));
        }
        d
    }
}
