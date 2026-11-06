# NetSurf for XenevaOS (bootstrap + CSS)

Staging browser frontend with a real CSS engine (NetSurf's libcss),
not upstream NetSurf core yet (no DOM/layout/JS/TLS).

## What it is

`netsurf.exe`: Chitralekha window, URL bar + Go button, HTTP/1.1 fetch over
XEClib sockets (same helper as `Process/http/curl.exe`). The bar opens on
`https://example.com/`. A host typed without a scheme is fetched as
`https://`; `http://` stays plain HTTP. TLS is 1.2 and 1.3 with SNI, ALPN
`http/1.1`, and a checked certificate chain (`Ports/mbedtls`). A fetched
HTTPS page starts with an `HTTPS` line. Readability-mode rendering with
computed styles.
Build with `make llvm` (needs `Ports/CssLibs/libcssport.a` and
`Ports/mbedtls/libmbedtls.a` first); the binary drops into
`Resources/resources/netsurf.exe` for initrd packing.

Keys: type/Backspace/Enter in the URL bar, Left/Right/Home/End move the
caret (block caret, drawn by the URL bar's custom paint handler), Up/Down
PgUp/PgDn scroll the page (line-windowed slice of the fetched text).

Styling (`css.c`/`css.h` select client over `Ports/CssLibs`): every element
open selects tag/class/id/descendant/inheritance rules from a UA sheet plus
page `<style>` blocks plus inline `style=""`. Applied today: `color`,
`font-size` (normal/large buckets), vertical `margin` spacing. UA defaults
give white body text, sized headings, blue links. Still text-only layout:
no CSS box model, images, or JS. HTTPS pages are fetched, then shown as text.

Like `doom.exe`, the built binary is a local artifact (`*.exe` is
git-ignored). `Scripts/Linux/build_and_run_qemu.sh` packs it by default and
`--no-netsurf` excludes it, mirroring `--no-doom`.

## What it is not (yet)

- No layout engine and no JS. CSS is the select client already in `css.c`.
- Page cap is 256 KiB of response body, text only.
- Certificate failure, a clock still at the epoch, and a missing virtio-rng
  all fail the HTTPS fetch. Plain `http://` does not need them.

## Upgrade path

1. Shim musl syscalls so upstream NetSurf support libs compile.
2. Port `libdom`, swap the text renderer for a real layout box tree.
