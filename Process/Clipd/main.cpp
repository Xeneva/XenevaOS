/*
 * clipd -- the guest half of the host<->VM clipboard.
 *
 * Text crosses the virtio-console port QEMU exposes as a socket on the host
 * (127.0.0.1:43211 unless moved). A stream socket hands back whatever the
 * last write put in, with no boundaries of its own, so both ends frame it:
 *
 *     4 bytes  payload length, little endian
 *     1 byte   type -- 0 = clipboard contents, 1 = hello
 *     n bytes  payload
 *
 * The hello is why the type exists. It goes out before anything else and is
 * retried until a write reports bytes accepted, which is the only signal
 * this side has that a host is attached at all (port_fops_read answers 0 and
 * -EAGAIN alike once DCL's wrapper has mapped both to 0). Until it lands,
 * the host has no reason to send either: QEMU drops what arrives before the
 * guest's port is open, and a host that pushed first would be pushing into
 * nothing.
 *
 * The bridge is one-directional at each edge and always carries both:
 * whatever arrives is written into /dev/clipboard, and whatever appears in
 * /dev/clipboard is queued to go out. Which of the two the host acts on is
 * the host's --dir choice, so this side takes no configuration and one image
 * serves a pull-only and a bidirectional setup alike.
 *
 * Every buffer is static: main()'s stack is small, and a 4 KB scratch pair
 * would sit in it for the life of the process.
 */

#include <_xeneva.h>
#include <stdio.h>
#include <string.h>
#include <sys/_kefile.h>
#include <sys/_keproc.h>
#include <Fs/Dev/devclip.h>

#define CLIP_TICK_MS 10
/* Repeating the hello after this many quiet ticks (200 x 10 ms = 2 s). The
 * guest cannot observe a host attach -- QEMU's PORT_OPEN is the only thing
 * that opens the write side and nothing here can see it -- so silence is the
 * only signal that a bridge which attached after boot is still waiting for a
 * word that was spent at startup. */
#define CLIP_HELLO_TICKS 200
#define CLIP_HDR 5 /* length (4) + type (1) */
#define CLIP_TYPE_TEXT 0
#define CLIP_TYPE_HELLO 1

static int g_kmsg = -1;
static int g_clip_rd = -1;
static int g_clip_wr = -1;
static int g_port = -1;

static uint8_t s_rx[CLIP_HDR + CLIPBOARD_MAX];
static uint8_t s_tx[CLIP_HDR + CLIPBOARD_MAX];
static uint8_t s_clip[CLIPBOARD_MAX];
static uint8_t s_last[CLIPBOARD_MAX];
static uint32_t s_rx_have;
static uint32_t s_tx_have;
static uint32_t s_tx_sent;
static uint32_t s_tx_len;
static uint32_t s_last_len;
static uint32_t s_quiet_ticks;
static int s_hello_logged;

/* clipd_log -- one line to /dev/kmsg, which is what the boot log is read
 * from (this process is handed no console fd, same as dcltest). */
static void clipd_log(const char* line) {
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

/*
 * clipd_open_port -- find and open the virtio-console port
 *
 * The node name is not fixed: register_virtio_port() builds "vport%up%u"
 * from the slot the kernel gave the device and the port id the host sends
 * in PORT_ADD, so which one it is depends on what registered first.
 * Tried in order rather than derived, because nothing here can walk /dev.
 */
/* pause between port probes while none is found */
static uint64_t s_probe_ms = 500;

static int clipd_open_port(void) {
	static const char* names[] = {"/dev/vport0p1", "/dev/vport1p1",
								  "/dev/vport2p1", "/dev/vport3p1"};
	for (unsigned int i = 0; i < sizeof names / sizeof names[0]; i++) {
		int fd = _KeOpenFile((char*)names[i], FILE_OPEN_READ_ONLY);
		if (fd >= 0)
			return fd;
	}
	return -1;
}

/* clipd_queue -- stage one frame for the transmit path */
static void clipd_queue(uint8_t type, const uint8_t* data, uint32_t len) {
	if (s_tx_sent != s_tx_have)
		return; /* one frame in flight; the caller retries next tick */
	s_tx[0] = (uint8_t)(len & 0xff);
	s_tx[1] = (uint8_t)((len >> 8) & 0xff);
	s_tx[2] = (uint8_t)((len >> 16) & 0xff);
	s_tx[3] = (uint8_t)((len >> 24) & 0xff);
	s_tx[4] = type;
	for (uint32_t i = 0; i < len; i++)
		s_tx[CLIP_HDR + i] = data[i];
	s_tx_have = CLIP_HDR + len;
	s_tx_sent = 0;
	s_tx_len = len;
}

/* clipd_apply -- one frame from the host becomes the clipboard */
static void clipd_apply(const uint8_t* data, uint32_t len) {
	char line[200];

	if (g_clip_wr < 0)
		return;
	if (len == 0) {
		/* A zero-length write never reaches the device -- WriteFile returns
		 * early on !length -- so clearing has to be the clipboard's ioctl. */
		_KeFileIoControl(g_clip_wr, CLIP_IOCODE_CLEAR, 0);
		clipd_log("[clipd] host cleared the clipboard\r\n");
		return;
	}
	int wrote = (int)_KeWriteFile(g_clip_wr, (void*)data, len);
	sprintf(line, "[clipd] host -> clipboard %d bytes (%d written)\r\n", (int)len,
			wrote);
	clipd_log(line);
}

/* clipd_rx -- turn whatever has arrived into frames, greedily */
static void clipd_rx(int have) {
	s_rx_have += (uint32_t)have;

	while (s_rx_have >= CLIP_HDR) {
		uint32_t len = (uint32_t)s_rx[0] | ((uint32_t)s_rx[1] << 8) |
					   ((uint32_t)s_rx[2] << 16) | ((uint32_t)s_rx[3] << 24);
		uint8_t type = s_rx[4];
		if (len > CLIPBOARD_MAX) {
			/* Not a frame we made: drop the lot rather than walk bytes
			 * looking for a sync point that is not there. */
			s_rx_have = 0;
			return;
		}
		if (s_rx_have < CLIP_HDR + len)
			return;
		if (type == CLIP_TYPE_TEXT)
			clipd_apply(s_rx + CLIP_HDR, len);
		else if (type == CLIP_TYPE_HELLO)
			clipd_log("[clipd] host attached\r\n");

		uint32_t used = CLIP_HDR + len;
		/* Shift the remainder down. Destination is below source, so a
		 * forward byte copy is the overlap-safe direction. */
		for (uint32_t i = 0; i + used < s_rx_have; i++)
			s_rx[i] = s_rx[i + used];
		s_rx_have -= used;
	}
}

int main(int argc, char* argv[]) {
	char line[200];

	(void)argc;
	(void)argv;

	g_clip_rd = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_READ_ONLY);
	g_clip_wr = _KeOpenFile((char*)"/dev/clipboard", FILE_OPEN_WRITE);
	g_port = clipd_open_port();

	/* Hello first: queued, not sent, so the transmit path below delivers it
	 * exactly as it delivers everything else -- retried until a write takes
	 * it, which is what tells the host a guest is listening. */
	clipd_queue(CLIP_TYPE_HELLO, 0, 0);

	sprintf(line, "[clipd] ready clipboard r=%d w=%d port=%d\r\n", g_clip_rd,
			g_clip_wr, g_port);
	clipd_log(line);

	while (1) {
		/* The port node may not exist yet -- PORT_ADD arrives from the
		 * host's control queue -- so keep looking until it does. On a board
		 * with no virtio-serial it never will, so the gap widens rather
		 * than spinning on a lookup that cannot win. */
		if (g_port < 0)
			g_port = clipd_open_port();

		/* host -> clipboard */
		if (g_port >= 0) {
			if (s_rx_have >= sizeof s_rx)
				s_rx_have = 0;
			int n = (int)_KeReadFile(g_port, s_rx + s_rx_have,
									 (int)(sizeof s_rx - s_rx_have));
			if (n > 0) {
				clipd_rx(n);
				s_quiet_ticks = 0;
			}
		}

		/* The host has said nothing for a while, so ask again. The first
		 * hello went out at boot and the host reads it, but a bridge that
		 * attaches later arrives after it is spent, and there is no packet
		 * from the host that could announce one -- so silence is what a
		 * waiting bridge looks like from here. */
		if (++s_quiet_ticks >= CLIP_HELLO_TICKS) {
			s_quiet_ticks = 0;
			clipd_queue(CLIP_TYPE_HELLO, 0, 0);
		}

		/* clipboard -> host: only once the previous frame has gone out and
		 * only when the contents differ from the last one sent. The host
		 * suppresses its own echo, which is what keeps a round trip from
		 * turning into an endless one. */
		if (g_port >= 0 && g_clip_rd >= 0 && s_tx_have == s_tx_sent &&
			s_tx_len != 0) {
			int n = (int)_KeReadFile(g_clip_rd, s_clip, CLIPBOARD_MAX);
			if (n > 0 && ((uint32_t)n != s_last_len ||
						  memcmp(s_clip, s_last, n) != 0)) {
				clipd_queue(CLIP_TYPE_TEXT, s_clip, (uint32_t)n);
				for (int i = 0; i < n; i++)
					s_last[i] = s_clip[i];
				s_last_len = (uint32_t)n;
			}
		}

		if (g_port >= 0 && s_tx_sent < s_tx_have) {
			int w = (int)_KeWriteFile(g_port, s_tx + s_tx_sent,
									  s_tx_have - s_tx_sent);
			if (w > 0)
				s_tx_sent += (uint32_t)w;
			if (s_tx_sent == s_tx_have) {
				if (s_tx_len == 0) {
					/* Only the first one is news: the repeats exist purely to
					 * be heard by a bridge that attached later, and logging
					 * them would bury the boot markers under them. */
					if (!s_hello_logged) {
						s_hello_logged = 1;
						clipd_log("[clipd] hello sent\r\n");
					}
				} else {
					sprintf(line, "[clipd] clipboard -> host %d bytes\r\n",
							(int)s_tx_len);
					clipd_log(line);
				}
				s_tx_have = 0;
				s_tx_sent = 0;
				s_tx_len = 0xffffffffu; /* no clipboard frame queued yet */
			}
		}

		/* no port (e.g. no virtio-serial device at all, as on the
		 * Raspberry Pi): look again with a growing pause, up to 10 s,
		 * instead of four failed opens every 500 ms */
		if (g_port >= 0) {
			s_probe_ms = 500;
			_KeProcessSleep(CLIP_TICK_MS);
		} else {
			_KeProcessSleep(s_probe_ms);
			if (s_probe_ms < 10000)
				s_probe_ms *= 2;
		}
	}

	return 0;
}
