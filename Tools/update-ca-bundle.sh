#!/bin/sh
# Refresh the Mozilla CA bundle used by mbedTLS and the HTTPS client.
#
# The build generates the embedded fallback (obj/cacert.c) from this PEM and
# copies it into the image as /etc/ssl/certs/cacert.pem. Override the source
# URL with the first argument if a mirror is needed.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
url=${1:-https://curl.se/ca/cacert.pem}
tmp=$(mktemp "${TMPDIR:-/tmp}/xeneva-cacert.XXXXXX")
trap 'rm -f "$tmp"' EXIT HUP INT TERM

if ! command -v curl >/dev/null 2>&1; then
    echo "update-ca-bundle: curl is required" >&2
    exit 1
fi

curl -fsSL --max-time 60 "$url" -o "$tmp"
if ! grep -q "BEGIN CERTIFICATE" "$tmp"; then
    echo "update-ca-bundle: $url did not return a PEM bundle" >&2
    exit 1
fi

count=$(grep -c "BEGIN CERTIFICATE" "$tmp")
cp -f "$tmp" "$root/Ports/mbedtls/certs/cacert.pem"
printf 'updated %s (%s certificates)\n' \
    "$root/Ports/mbedtls/certs/cacert.pem" "$count"
printf 'the next build regenerates the embedded fallback and installs the bundle at /etc/ssl/certs/cacert.pem\n'
