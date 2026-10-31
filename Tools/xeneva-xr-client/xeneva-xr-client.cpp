// xeneva-xr-client: interactive client for the QEMU monitor socket.
//
// The XR demo runs QEMU without a local window (dbus display + VNC), so the
// bootloader menus and scripted input go through the monitor. This tool
// behaves like the monitor itself: type HMP commands, they run in QEMU.
// (For point-and-click, use gvncviewer on the VNC display instead.)
//
//   xeneva-xr-client [--monitor PATH]
//   xeneva-xr-client [--monitor PATH] --exec "sendkey ret"
//
// Local dot-commands (not sent to QEMU):
//   .help          this help
//   .quit          disconnect
//   .key NAME      sendkey NAME      (e.g. .key ret, .key down)
//   .boot          sendkey ret       (pick the default bootloader entry)
//
// Everything else is forwarded to the QEMU monitor verbatim, so all of
// `help`, `info status`, `sendkey`, `screendump`, ... work as usual.
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

constexpr char kDefaultMonitor[] = "/tmp/qemu-mon.sock";

int connect_monitor(const char* path) {
	int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		std::perror("socket");
		return -1;
	}
	sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
	if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		std::fprintf(stderr, "xeneva-xr-client: connect %s: %s\n", path, strerror(errno));
		close(fd);
		return -1;
	}
	return fd;
}

// Read until the monitor prompt "(qemu) " shows up or the timeout expires.
// Returns false on timeout / disconnect.
bool drain_until_prompt(int fd, int timeout_ms) {
	std::string tail;
	char buf[4096];
	int waited = 0;
	while (waited < timeout_ms) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		timeval tv{0, 50000};
		int r = select(fd + 1, &rfds, nullptr, nullptr, &tv);
		waited += 50;
		if (r < 0)
			return false;
		if (r == 0)
			continue;
		ssize_t n = read(fd, buf, sizeof(buf));
		if (n <= 0)
			return false;
		fwrite(buf, 1, (size_t)n, stdout);
		fflush(stdout);
		tail.append(buf, (size_t)n);
		if (tail.size() > 32)
			tail.erase(0, tail.size() - 32);
		if (tail.find("(qemu) ") != std::string::npos)
			return true;
	}
	return false;
}

void send_line(int fd, const std::string& line) {
	std::string out = line + "\n";
	size_t done = 0;
	while (done < out.size()) {
		ssize_t n = write(fd, out.data() + done, out.size() - done);
		if (n <= 0)
			break;
		done += (size_t)n;
	}
}

int run_exec(int fd, const std::string& cmd) {
	if (!drain_until_prompt(fd, 5000)) {
		std::fprintf(stderr, "xeneva-xr-client: no monitor banner\n");
		return 1;
	}
	send_line(fd, cmd);
	if (!drain_until_prompt(fd, 15000)) {
		std::fprintf(stderr, "xeneva-xr-client: no reply (command may still have run)\n");
		return 1;
	}
	return 0;
}

void print_help() {
	std::fprintf(stderr,
				 "xeneva-xr-client [--monitor PATH] [--exec CMD...]\n"
				 "Interactive QEMU monitor client for the XR demo.\n"
				 "  .help      this help\n"
				 "  .quit      disconnect\n"
				 "  .key NAME  sendkey NAME  (e.g. .key ret, .key up/down)\n"
				 "  .boot      sendkey ret (default bootloader entry)\n"
				 "Anything else is sent to the QEMU monitor verbatim.\n");
}

int run_interactive(int fd) {
	if (!drain_until_prompt(fd, 5000))
		std::fprintf(stderr, "xeneva-xr-client: (no banner yet, continuing)\n");
	print_help();
	bool tty = isatty(STDIN_FILENO) != 0;
	std::string pending;
	char buf[4096];
	while (true) {
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		FD_SET(STDIN_FILENO, &rfds);
		int r = select(std::max(fd, STDIN_FILENO) + 1, &rfds, nullptr, nullptr, nullptr);
		if (r < 0)
			break;
		if (FD_ISSET(fd, &rfds)) {
			ssize_t n = read(fd, buf, sizeof(buf));
			if (n <= 0) {
				std::fprintf(stderr, "\nxeneva-xr-client: monitor closed\n");
				return 0;
			}
			fwrite(buf, 1, (size_t)n, stdout);
			fflush(stdout);
		}
		if (FD_ISSET(STDIN_FILENO, &rfds)) {
			ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
			if (n <= 0)
				return 0;
			pending.append(buf, (size_t)n);
			size_t pos;
			while ((pos = pending.find('\n')) != std::string::npos) {
				std::string line = pending.substr(0, pos);
				pending.erase(0, pos + 1);
				if (!line.empty() && line.back() == '\r')
					line.pop_back();
				if (line == ".quit")
					return 0;
				if (line == ".help") {
					print_help();
					continue;
				}
				if (line == ".boot") {
					send_line(fd, "sendkey ret");
					continue;
				}
				if (line.rfind(".key ", 0) == 0) {
					send_line(fd, "sendkey " + line.substr(5));
					continue;
				}
				if (!line.empty() && line[0] == '.' && tty) {
					std::fprintf(stderr, "unknown local command (try .help)\n");
					continue;
				}
				send_line(fd, line);
			}
		}
	}
	return 0;
}

}  // namespace

int main(int argc, char** argv) {
	const char* monitor = kDefaultMonitor;
	std::string exec;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--monitor") && i + 1 < argc) {
			monitor = argv[++i];
		} else if (!strcmp(argv[i], "--exec") && i + 1 < argc) {
			exec.clear();
			for (i++; i < argc; i++) {
				if (!exec.empty())
					exec += " ";
				exec += argv[i];
			}
		} else {
			std::fprintf(stderr, "usage: xeneva-xr-client [--monitor PATH] [--exec CMD...]\n");
			return 1;
		}
	}
	int fd = connect_monitor(monitor);
	if (fd < 0)
		return 1;
	int rc = exec.empty() ? run_interactive(fd) : run_exec(fd, exec);
	close(fd);
	return rc;
}
