/*
 * Process/bttest/main.cpp -- user-space end-to-end test for /dev/bt0.
 *
 * init spawns this when bttest.exe is in the image, the same opt-in-by-
 * presence rule as hwtest.exe. It drives the real ioctl path a user
 * application uses (btctl is the shipped client) against the in-guest
 * mock controller that Bt/host.c installs when no UART1 bridge or USB
 * dongle is present:
 *
 *   identity -> LE scan -> connect -> ATT Device Name -> LE audio ->
 *   disconnect
 *
 * The mock is recognised by its fixed BD_ADDR. On a real adapter the
 * test reports "non-mock adapter" and exits 0: the address, advertiser
 * list and codec support there depend on the environment, and this is
 * the deterministic QEMU gate. Pairing is deliberately not exercised:
 * the mock has no SMP responder (do_pair says so in the serial log).
 *
 * Every result goes to stdout and /dev/kmsg, so CI reads it from the
 * QEMU serial log:
 *     grep '\[bttest\]' run.log
 * Exit code is the number of failed checks.
 */
#include <_xeneva.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/_kefile.h>
#include <Bt/bt.h>

static int g_ok;
static int g_bad;
static int g_kmsg = -1;

static void emit(const char* line) {
	printf("%s", line);
	if (g_kmsg < 0)
		g_kmsg = _KeOpenFile((char*)"/dev/kmsg", FILE_OPEN_WRITE);
	if (g_kmsg >= 0)
		_KeWriteFile(g_kmsg, (void*)line, strlen(line));
}

static void report(const char* what, long got, long want) {
	char line[168];
	if (got == want) {
		g_ok++;
		sprintf(line, "[bttest] PASS %s (%ld)\n", what, got);
	} else {
		g_bad++;
		sprintf(line, "[bttest] FAIL %s got=%ld want=%ld\n", what, got, want);
	}
	emit(line);
}

static void note(const char* what) {
	char line[168];
	sprintf(line, "[bttest] info %s\n", what);
	emit(line);
}

/* SUMMARY goes through /dev/kmsg too: the process is spawned without a
 * console fd, so a plain printf never reaches the serial log. */
static void summary(void) {
	char line[96];
	sprintf(line, "[bttest] SUMMARY %d ok, %d failed\n", g_ok, g_bad);
	emit(line);
}

/* The mock controller's fixed identity, in the byte order the kernel
 * keeps (LSB first) and the order btctl prints (MSB first). */
static const unsigned char mock_addr[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const char mock_addr_text[] = "01:00:00:00:00:02";

static int scan_find_name(const BtInfo* info, const char* name) {
	int i;
	for (i = 0; i < info->nscan && i < BT_MAX_SCAN; i++)
		if (!strcmp(info->scan[i].name, name))
			return i;
	return -1;
}

int main(int argc, char* argv[]) {
	BtInfo* info;
	char text[128];
	int fd;
	int st;
	int idx;
	unsigned char peer[6];
	unsigned char peer_type;
	size_t n;
	(void)argc;
	(void)argv;

	fd = _KeOpenFile((char*)"/dev/bt0", FILE_OPEN_READ_ONLY | FILE_OPEN_WRITE);
	if (fd < 0) {
		note("no /dev/bt0, skipped");
		summary();
		return 0;
	}
	report("bt open /dev/bt0", 1, 1);

	info = (BtInfo*)malloc(sizeof *info);
	if (!info) {
		emit("[bttest] FAIL bt info allocation\n");
		g_bad++;
		summary();
		_KeCloseFile(fd);
		return 1;
	}
	memset(info, 0, sizeof *info);

	st = _KeFileIoControl(fd, BT_GET_INFO, info);
	report("bt info status", st, BT_OK);
	report("bt mock address", memcmp(info->bd_addr, mock_addr, 6) == 0, 1);
	if (memcmp(info->bd_addr, mock_addr, 6) != 0) {
		note("non-mock adapter, scan/connect/audio skipped");
		free(info);
		_KeCloseFile(fd);
		summary();
		return g_bad;
	}

	/* The read side is what btctl info's counterpart exposes to a plain
	 * read(); assert it agrees with the ioctl identity. */
	memset(text, 0, sizeof text);
	n = _KeReadFile(fd, text, sizeof text - 1);
	report("bt read returns identity",
		   n > 0 && !strncmp(text, mock_addr_text, strlen(mock_addr_text)), 1);
	report("bt read reports iso", strstr(text, "iso=1") != NULL, 1);

	report("bt hci version", info->hci_ver, 9);
	report("bt iso support", info->iso, 1);
	report("bt iso mtu", info->iso_mtu, 40);

	/* The boot-time scan already filled the table; scan again through the
	 * ioctl to prove the user ABI, and expect both mock advertisers. */
	st = _KeFileIoControl(fd, BT_SCAN, info);
	report("bt scan status", st, BT_OK);
	report("bt scan advertisers", info->nscan, 2);
	report("bt scan finds XenevaSink", scan_find_name(info, "XenevaSink") >= 0, 1);
	report("bt scan finds XenevaHead", scan_find_name(info, "XenevaHead") >= 0, 1);

	idx = scan_find_name(info, "XenevaSink");
	if (idx < 0) {
		note("XenevaSink missing, connect/name/audio skipped");
		free(info);
		_KeCloseFile(fd);
		summary();
		return g_bad;
	}
	memcpy(peer, info->scan[idx].addr, 6);
	peer_type = info->scan[idx].addr_type;

	memset(info, 0, sizeof *info);
	memcpy(info->peer, peer, 6);
	info->peer_type = peer_type;
	st = _KeFileIoControl(fd, BT_CONNECT, info);
	report("bt connect status", st, BT_OK);
	report("bt connected", info->connected, 1);
	report("bt connect peer", memcmp(info->peer, peer, 6) == 0, 1);

	st = _KeFileIoControl(fd, BT_READ_NAME, info);
	report("bt name status", st, BT_OK);
	report("bt name XenevaSink", !strcmp(info->peer_name, "XenevaSink"), 1);

	st = _KeFileIoControl(fd, BT_AUDIO, info);
	report("bt audio status", st, BT_OK);
	report("bt audio card", info->card_id >= 0, 1);
	report("bt audio name ble0", !strcmp(info->msg, "ble0"), 1);

	st = _KeFileIoControl(fd, BT_DISCONNECT, info);
	report("bt disconnect status", st, BT_OK);
	report("bt disconnected", info->connected, 0);

	free(info);
	_KeCloseFile(fd);
	summary();
	return g_bad;
}
