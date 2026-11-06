#!/usr/bin/env python3
"""Turn cacert.pem into a NUL-terminated C array for mbedtls_x509_crt_parse."""
import sys

def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, "rb").read()
    if not data.endswith(b"\0"):
        data += b"\0"
    with open(dst, "w", encoding="ascii") as out:
        out.write("/* Generated from certs/cacert.pem. Do not edit. */\n")
        out.write("const unsigned char xe_cacert_pem[] = {\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i + 16]
            out.write("  " + ",".join(str(b) for b in chunk) + ",\n")
        out.write("};\n")
        out.write("const unsigned int xe_cacert_pem_len = %u;\n" % len(data))

if __name__ == "__main__":
    main()
