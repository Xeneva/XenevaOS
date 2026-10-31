# Telnet for XenevaOS

Terminal telnet client (plain NVT, no TLS/SSH).

## Usage

```
telnet <host> [port]      (default 23, host:port accepted)
```

`Ctrl+C` or `Ctrl+]` quits. Note: the Xeneva Terminal delivers Ctrl+C
to the foreground job as SIGINT (xesh forwards it), so it can never
travel in-band to the server -- telnet treats it as a graceful local
quit instead. Close detection relies on the kernel returning 0 on
FIN/close and -1 on an empty RX queue (`AuTCPReceive` semantics).

## Protocol

Replies `WONT` to every `DO` and `DONT` to every `WILL`, except:
- `SGA` (suppress-go-ahead): `WILL` (we already send char-at-a-time).
- `TTYPE`: `WILL`, answering `SEND` with `"VT100"` to match the Xeneva
  Terminal's ANSI/VT100 support.

`IAC IAC` decodes to a literal 255; other `IAC` commands are ignored.
Keyboard bytes pass through raw, with `IAC` escaped on send.

## Threads

Two threads: main pumps network→TTY, a helper (`_KeCreateThread`)
pumps TTY→network. Either side ending closes the socket and exits.
