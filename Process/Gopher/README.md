# Gopher for XenevaOS

Terminal gopher client (RFC 1436) with interactive link following.

## Usage

```
gopher <host> [port] [selector]   (default port 70)
```

Menus render with numbered links (`/` marks directories, `(search)`
marks type-7). At the `gopher>` prompt:

- `<number>` follows the link (type 7 prompts for a search query,
  sent as `selector<TAB>query`).
- `u <host>` switches servers.
- `q` (or empty line... no: empty refetches? `q`) quits; anything else
  is fetched as a new selector on the same host.

`+` hosts and missing ports inherit the current server, per convention.
Text bodies (no tabs) print as-is. 32 KiB page cap.
