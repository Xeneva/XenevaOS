#ifndef _LINUX_TTY_H
#define _LINUX_TTY_H

/*
 * DCL <linux/tty.h> -- struct tty_struct and the tty core's public surface.
 *
 * Ported from mainline v7.2. This used to declare exactly one thing (tty_init,
 * the placeholder in DCL/linux_mm_shim.c); it is now the real header, because
 * tty_buffer.c is the first staged file that needs a tty to look like a tty.
 *
 * Three deliberate departures, each for a reason that is visible in the code:
 *
 *  1. The CONFIG_TTY / CONFIG_AUDIT conditionals are written out in the
 *     "enabled" direction. DCL's configuration lives in <linux/autoconf.h>
 *     and has no CONFIG_TTY -- it was never needed, because tty_init() was a
 *     stub. Writing only the enabled branch means the declarations that
 *     tty_io.c and serial_core.c call always exist, instead of silently
 *     becoming static inline stubs that return -ENODEV. CONFIG_AUDIT's block
 *     is written out as the "not enabled" (inline no-op) branch: there is no
 *     audit subsystem here at all, so tty_audit_* must be a no-op rather than
 *     a link error.
 *
 *  2. tty_termios_baud_rate()/tty_termios_encode_baud_rate()/tty_termios_
 *     copy_hw() are *not* re-declared here. mainline declares them in this
 *     header and defines them in drivers/tty/tty_termios.c; DCL defines them
 *     as static inline in <linux/linux/termios.h> (one shared table, both
 *     directions). An external declaration next to a static inline definition
 *     of the same name is a compile error -- "non-static declaration follows
 *     static declaration" -- so the declaration goes away and the definition
 *     stays where it can be shared by every caller.
 *
 *  3. The N_* line discipline numbers come from uapi/linux/tty.h in mainline.
 *     DCL has no uapi layer, so they live here. n_tty.c and tty_ldisc.c are
 *     the only staged files that need them, and both include this header.
 *
 * The struct below is mainline's, field for field, including the two anonymous
 * sub-structs (flow, ctrl) -- they are grouped exactly as mainline groups them
 * because tty_io.c, n_tty.c and serial_core.c all reach for them by member
 * name, and a field that is present but differently named is indistinguishable
 * from a missing one at compile time until the error message.
 */

#include <linux/fs.h>
#include <linux/termios.h>
/* O_NONBLOCK, used by tty_io_nonblock() below */
#include <linux/fcntl.h>
#include <linux/workqueue.h>
#include <linux/tty_driver.h>
#include <linux/tty_ldisc.h>
#include <linux/tty_port.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/capability.h>

/*
 * Line disciplines. mainline: include/uapi/linux/tty.h. NR_LDISCS is one past
 * the newest -- tty_ldisc.c sizes its ops array with it, so a DCL that grows a
 * discipline has to grow this too.
 */
#define N_TTY		0
#define N_SLIP		1
#define N_MOUSE		2
#define N_PPP		3
#define N_STRIP		4
#define N_AX25		5
#define N_X25		6
#define N_6PACK		7
#define N_MASC		8
#define N_R3964		9
#define N_PROFIBUS_FDL	10
#define N_IRDA		11
#define N_SMSBLOCK	12
#define N_HDLC		13
#define N_SYNC_PPP	14
#define N_HCI		15
#define N_GIGASET_M101	16
#define N_SLCAN		17
#define N_PPS		18
#define N_V253		19
#define N_CAIF		20
#define N_GSM0710	21
#define N_TI_WL		22
#define N_TRACESINK	23
#define N_TRACEROUTER	24
#define N_NCI		25
#define N_SPEAKUP	26
#define N_NULL		27
#define N_MCTP		28
#define N_DEVELOPMENT	29
#define N_CAN327	30
#define NR_LDISCS	31

/*
 * This character is the same as _POSIX_VDISABLE: it cannot be used as
 * a c_cc[] character, but indicates that a particular special character
 * isn't in use (eg VINTR has no character etc)
 */
#define __DISABLED_CHAR '\0'

#define INTR_CHAR(tty) ((tty)->termios.c_cc[VINTR])
#define QUIT_CHAR(tty) ((tty)->termios.c_cc[VQUIT])
#define ERASE_CHAR(tty) ((tty)->termios.c_cc[VERASE])
#define KILL_CHAR(tty) ((tty)->termios.c_cc[VKILL])
#define EOF_CHAR(tty) ((tty)->termios.c_cc[VEOF])
#define TIME_CHAR(tty) ((tty)->termios.c_cc[VTIME])
#define MIN_CHAR(tty) ((tty)->termios.c_cc[VMIN])
#define SWTC_CHAR(tty) ((tty)->termios.c_cc[VSWTC])
#define START_CHAR(tty) ((tty)->termios.c_cc[VSTART])
#define STOP_CHAR(tty) ((tty)->termios.c_cc[VSTOP])
#define SUSP_CHAR(tty) ((tty)->termios.c_cc[VSUSP])
#define EOL_CHAR(tty) ((tty)->termios.c_cc[VEOL])
#define REPRINT_CHAR(tty) ((tty)->termios.c_cc[VREPRINT])
#define DISCARD_CHAR(tty) ((tty)->termios.c_cc[VDISCARD])
#define WERASE_CHAR(tty) ((tty)->termios.c_cc[VWERASE])
#define LNEXT_CHAR(tty)	((tty)->termios.c_cc[VLNEXT])
#define EOL2_CHAR(tty) ((tty)->termios.c_cc[VEOL2])

#define _I_FLAG(tty, f)	((tty)->termios.c_iflag & (f))
#define _O_FLAG(tty, f)	((tty)->termios.c_oflag & (f))
#define _C_FLAG(tty, f)	((tty)->termios.c_cflag & (f))
#define _L_FLAG(tty, f)	((tty)->termios.c_lflag & (f))

#define I_IGNBRK(tty)	_I_FLAG((tty), IGNBRK)
#define I_BRKINT(tty)	_I_FLAG((tty), BRKINT)
#define I_IGNPAR(tty)	_I_FLAG((tty), IGNPAR)
#define I_PARMRK(tty)	_I_FLAG((tty), PARMRK)
#define I_INPCK(tty)	_I_FLAG((tty), INPCK)
#define I_ISTRIP(tty)	_I_FLAG((tty), ISTRIP)
#define I_INLCR(tty)	_I_FLAG((tty), INLCR)
#define I_IGNCR(tty)	_I_FLAG((tty), IGNCR)
#define I_ICRNL(tty)	_I_FLAG((tty), ICRNL)
#define I_IUCLC(tty)	_I_FLAG((tty), IUCLC)
#define I_IXON(tty)	_I_FLAG((tty), IXON)
#define I_IXANY(tty)	_I_FLAG((tty), IXANY)
#define I_IXOFF(tty)	_I_FLAG((tty), IXOFF)
#define I_IMAXBEL(tty)	_I_FLAG((tty), IMAXBEL)
#define I_IUTF8(tty)	_I_FLAG((tty), IUTF8)

#define O_OPOST(tty)	_O_FLAG((tty), OPOST)
#define O_OLCUC(tty)	_O_FLAG((tty), OLCUC)
#define O_ONLCR(tty)	_O_FLAG((tty), ONLCR)
#define O_OCRNL(tty)	_O_FLAG((tty), OCRNL)
#define O_ONOCR(tty)	_O_FLAG((tty), ONOCR)
#define O_ONLRET(tty)	_O_FLAG((tty), ONLRET)
#define O_OFILL(tty)	_O_FLAG((tty), OFILL)
#define O_OFDEL(tty)	_O_FLAG((tty), OFDEL)
#define O_NLDLY(tty)	_O_FLAG((tty), NLDLY)
#define O_CRDLY(tty)	_O_FLAG((tty), CRDLY)
#define O_TABDLY(tty)	_O_FLAG((tty), TABDLY)
#define O_BSDLY(tty)	_O_FLAG((tty), BSDLY)
#define O_VTDLY(tty)	_O_FLAG((tty), VTDLY)
#define O_FFDLY(tty)	_O_FLAG((tty), FFDLY)

#define C_BAUD(tty)	_C_FLAG((tty), CBAUD)
#define C_CSIZE(tty)	_C_FLAG((tty), CSIZE)
#define C_CSTOPB(tty)	_C_FLAG((tty), CSTOPB)
#define C_CREAD(tty)	_C_FLAG((tty), CREAD)
#define C_PARENB(tty)	_C_FLAG((tty), PARENB)
#define C_PARODD(tty)	_C_FLAG((tty), PARODD)
#define C_HUPCL(tty)	_C_FLAG((tty), HUPCL)
#define C_CLOCAL(tty)	_C_FLAG((tty), CLOCAL)
#define C_CIBAUD(tty)	_C_FLAG((tty), CIBAUD)
#define C_CRTSCTS(tty)	_C_FLAG((tty), CRTSCTS)
#define C_CMSPAR(tty)	_C_FLAG((tty), CMSPAR)

#define L_ISIG(tty)	_L_FLAG((tty), ISIG)
#define L_ICANON(tty)	_L_FLAG((tty), ICANON)
#define L_XCASE(tty)	_L_FLAG((tty), XCASE)
#define L_ECHO(tty)	_L_FLAG((tty), ECHO)
#define L_ECHOE(tty)	_L_FLAG((tty), ECHOE)
#define L_ECHOK(tty)	_L_FLAG((tty), ECHOK)
#define L_ECHONL(tty)	_L_FLAG((tty), ECHONL)
#define L_NOFLSH(tty)	_L_FLAG((tty), NOFLSH)
#define L_TOSTOP(tty)	_L_FLAG((tty), TOSTOP)
#define L_ECHOCTL(tty)	_L_FLAG((tty), ECHOCTL)
#define L_ECHOPRT(tty)	_L_FLAG((tty), ECHOPRT)
#define L_ECHOKE(tty)	_L_FLAG((tty), ECHOKE)
#define L_FLUSHO(tty)	_L_FLAG((tty), FLUSHO)
#define L_PENDIN(tty)	_L_FLAG((tty), PENDIN)
#define L_IEXTEN(tty)	_L_FLAG((tty), IEXTEN)
#define L_EXTPROC(tty)	_L_FLAG((tty), EXTPROC)

struct device;
struct signal_struct;
struct tty_operations;
struct pid;
struct task_struct;
struct class;
struct fasync_struct;

/**
 * struct tty_struct - state associated with a tty while open
 *
 * The field comments are mainline's, abridged where the comment only restated
 * the field name. What is worth knowing before touching this struct:
 * `count` is openers, `kref` is references, and they are not the same number --
 * count reaching zero cancels work and drops a kref, but the tty is freed by
 * whoever held the last kref, which may be a different path entirely.
 */
struct tty_struct {
	struct kref kref;
	int index;
	struct device* dev;
	struct tty_driver* driver;
	struct tty_port* port;
	const struct tty_operations* ops;

	struct tty_ldisc* ldisc;
	struct ld_semaphore ldisc_sem;

	struct mutex atomic_write_lock;
	struct mutex legacy_mutex;
	struct mutex throttle_mutex;
	struct rw_semaphore termios_rwsem;
	struct mutex winsize_mutex;
	struct ktermios termios, termios_locked;
	char name[64];
	unsigned long flags;
	int count;
	unsigned int receive_room;
	struct winsize winsize;

	struct {
		spinlock_t lock;
		bool stopped;
		bool tco_stopped;
	} flow;

	struct {
		struct pid* pgrp;
		struct pid* session;
		spinlock_t lock;
		unsigned char pktstatus;
		bool packet;
	} ctrl;

	bool hw_stopped;
	bool closing;
	int flow_change;

	struct tty_struct* link;
	struct fasync_struct* fasync;
	wait_queue_head_t write_wait;
	wait_queue_head_t read_wait;
	struct work_struct hangup_work;
	void* disc_data;
	void* driver_data;
	spinlock_t files_lock;
	int write_cnt;
	u8* write_buf;

	struct list_head tty_files;

	struct work_struct SAK_work;
};

/* Each of a tty's open files has private data */
struct tty_file_private {
	struct tty_struct* tty;
	struct file* file;
	struct list_head list;
};

/* Bits in tty_struct::flags -- mainline's enum tty_struct_flags */
enum tty_struct_flags {
	TTY_THROTTLED,
	TTY_IO_ERROR,
	TTY_OTHER_CLOSED,
	TTY_EXCLUSIVE,
	TTY_DO_WRITE_WAKEUP,
	TTY_LDISC_OPEN,
	TTY_PTY_LOCK,
	TTY_NO_WRITE_SPLIT,
	TTY_HUPPED,
	TTY_HUPPING,
	TTY_LDISC_CHANGING,
	TTY_LDISC_HALTED,
};

static inline bool tty_io_nonblock(struct tty_struct* tty, struct file* file)
{
	return file->f_flags & O_NONBLOCK ||
		test_bit(TTY_LDISC_CHANGING, &tty->flags);
}

static inline bool tty_io_error(struct tty_struct* tty)
{
	return test_bit(TTY_IO_ERROR, &tty->flags);
}

static inline bool tty_throttled(struct tty_struct* tty)
{
	return test_bit(TTY_THROTTLED, &tty->flags);
}

/* --- tty_kref_get / tty_get_baud_rate: mainline keeps these inline ------ */

/**
 * tty_kref_get - get a tty reference
 *
 * Returns: a new reference to a tty object
 *
 * Locking: The caller must hold sufficient locks/counts to ensure that their
 * existing reference cannot go away.
 */
static inline struct tty_struct* tty_kref_get(struct tty_struct* tty)
{
	if (tty)
		kref_get(&tty->kref);
	return tty;
}

/**
 * tty_get_baud_rate - get tty bit rates
 *
 * Returns: the baud rate as an integer for this terminal
 *
 * Locking: The termios lock must be held by the caller.
 */
static inline speed_t tty_get_baud_rate(const struct tty_struct* tty)
{
	return tty_termios_baud_rate(&tty->termios);
}

/* --- tty core ----------------------------------------------------------- */

void tty_kref_put(struct tty_struct* tty);
struct pid* tty_get_pgrp(struct tty_struct* tty);
void tty_vhangup_self(void);
void disassociate_ctty(int priv);
dev_t tty_devnum(struct tty_struct* tty);
void proc_clear_tty(struct task_struct* p);
struct tty_struct* get_current_tty(void);

/* tty_io.c */
int tty_init(void);
const char* tty_name(const struct tty_struct* tty);
const char* tty_driver_name(const struct tty_struct* tty);
struct tty_struct* tty_kopen_exclusive(dev_t device);
struct tty_struct* tty_kopen_shared(dev_t device);
void tty_kclose(struct tty_struct* tty);
int tty_dev_name_to_number(const char* name, dev_t* number);

void tty_wait_until_sent(struct tty_struct* tty, long timeout);
void stop_tty(struct tty_struct* tty);
void start_tty(struct tty_struct* tty);
void tty_write_message(struct tty_struct* tty, char* msg);
int tty_send_xchar(struct tty_struct* tty, u8 ch);
int tty_put_char(struct tty_struct* tty, u8 c);
unsigned int tty_chars_in_buffer(struct tty_struct* tty);
unsigned int tty_write_room(struct tty_struct* tty);
void tty_driver_flush_buffer(struct tty_struct* tty);
void tty_unthrottle(struct tty_struct* tty);
bool tty_throttle_safe(struct tty_struct* tty);
bool tty_unthrottle_safe(struct tty_struct* tty);
int tty_do_resize(struct tty_struct* tty, struct winsize* ws);
int tty_get_icount(struct tty_struct* tty,
		struct serial_icounter_struct* icount);
int tty_get_tiocm(struct tty_struct* tty);
int is_current_pgrp_orphaned(void);
void tty_hangup(struct tty_struct* tty);
void tty_vhangup(struct tty_struct* tty);
int tty_hung_up_p(struct file* filp);
void do_SAK(struct tty_struct* tty);
void __do_SAK(struct tty_struct* tty);
void no_tty(void);
void tty_encode_baud_rate(struct tty_struct* tty, speed_t ibaud,
		speed_t obaud);

unsigned char tty_get_char_size(unsigned int cflag);
unsigned char tty_get_frame_size(unsigned int cflag);
bool tty_termios_hw_change(const struct ktermios* a, const struct ktermios* b);
int tty_set_termios(struct tty_struct* tty, struct ktermios* kt);

void tty_wakeup(struct tty_struct* tty);

int tty_mode_ioctl(struct tty_struct* tty, unsigned int cmd, unsigned long arg);
int tty_perform_flush(struct tty_struct* tty, unsigned long arg);
struct tty_struct* tty_init_dev(struct tty_driver* driver, int idx);
void tty_release_struct(struct tty_struct* tty, int idx);
void tty_init_termios(struct tty_struct* tty);
void tty_save_termios(struct tty_struct* tty);
int tty_standard_install(struct tty_driver* driver, struct tty_struct* tty);

extern struct mutex tty_mutex;

/* n_tty.c */
void n_tty_inherit_ops(struct tty_ldisc_ops* ops);
void n_tty_init(void);

/* tty_audit.c -- no audit subsystem in DCL: these are the inline no-ops
 * mainline writes under #else. */
static inline void tty_audit_exit(void) { }
static inline void tty_audit_fork(struct signal_struct* sig) { (void)sig; }
static inline int tty_audit_push(void) { return 0; }

/* tty_ioctl.c */
int n_tty_ioctl_helper(struct tty_struct* tty, unsigned int cmd,
		unsigned long arg);

/* vt.c */
int vt_ioctl(struct tty_struct* tty, unsigned int cmd, unsigned long arg);
long vt_compat_ioctl(struct tty_struct* tty, unsigned int cmd,
		unsigned long arg);

/* tty_mutex.c */
void tty_lock(struct tty_struct* tty);
int  tty_lock_interruptible(struct tty_struct* tty);
void tty_unlock(struct tty_struct* tty);
void tty_lock_slave(struct tty_struct* tty);
void tty_unlock_slave(struct tty_struct* tty);
void tty_set_lock_subclass(struct tty_struct* tty);

#endif /* _LINUX_TTY_H */
