#!/usr/bin/env python3
"""Align an APK's stored entries: 16 KB pages for .so files, 4 bytes else.

Replaces `zipalign -p/-P` here because the SDK binaries for this host
silently no-op on both flags (verified empirically: entry offsets do not move
and `zipalign -c` reports OK on a demonstrably misaligned archive).

Method: entries are rewritten in order with an extra-field padding block, so
each stored entry's data starts on its required boundary.  Compressed entries
cannot be aligned and are passed through untouched.  Extra fields already
present (e.g. aapt2's own resource alignment) are preserved and accounted
for.  CRCs cover file data only, so moving entries is signature-neutral --
but like zipalign this must still run *before* apksigner, which digests the
central directory.

Usage:  python3 tools/align_apk.py in.apk out.apk
"""

import struct
import sys
import zipfile

ALIGN_EXTRA_ID = 0xD935  # Android's zip-alignment extra field
LOCAL_HEADER_LEN = 30


def padded_extra(filename, extra, data_pos, page):
    """Return an extra field placing data on a `page` boundary."""
    base = data_pos + LOCAL_HEADER_LEN + len(filename.encode("utf-8"))
    pad = (page - ((base + len(extra) + 4) % page)) % page
    if pad == 0 and (base + len(extra)) % page == 0:
        return extra
    return extra + struct.pack("<HH", ALIGN_EXTRA_ID, pad) + b"\0" * pad


def align(src, dst):
    with zipfile.ZipFile(src, "r") as zin:
        entries = [(info, zin.read(info.filename)) for info in zin.infolist()]
        comment = zin.comment
    # Native libraries go right after the manifest, before anything apksigner
    # rewrites.  apksigner preserves large alignment blocks byte-for-byte but
    # re-pads small entries as it copies, so any .so after a rewritten entry
    # would drift off its page.  Up front, the only thing before them is the
    # manifest, which apksigner has never touched.
    def sort_key(pair):
        name = pair[0].filename
        if name == "AndroidManifest.xml":
            return (0, 0)
        if name.endswith(".so"):
            return (1, 0)
        return (2, 0)
    entries.sort(key=sort_key)
    with zipfile.ZipFile(dst, "w") as zout:
        zout.comment = comment
        for info, data in entries:
            ni = zipfile.ZipInfo(info.filename, date_time=info.date_time)
            ni.compress_type = info.compress_type
            ni.external_attr = info.external_attr
            ni.create_system = info.create_system
            extra = info.extra
            # Stored .so files need a 16 KB page for Android 15+ install checks;
            # everything else (including deflated data, which apksigner also
            # pads) needs 4-byte alignment so the signer appends META-INF and
            # nothing else.  Any byte it inserts before a .so would knock the
            # .so off its page.
            if info.compress_type == zipfile.ZIP_STORED:
                page = 16384 if info.filename.endswith(".so") else 4
            else:
                page = 4
            extra = padded_extra(info.filename, extra, zout.fp.tell(), page)
            ni.extra = extra
            if info.compress_type == zipfile.ZIP_DEFLATED:
                zout.writestr(ni, data, compresslevel=6)
            else:
                zout.writestr(ni, data)


def check(path):
    """True iff every entry meets its boundary.  Trusts nothing."""
    ok = True
    with zipfile.ZipFile(path, "r") as zin:
        for info in zin.infolist():
            # META-INF is added by apksigner after alignment; signature blobs
            # carry no alignment requirement.
            if info.filename.startswith("META-INF/"):
                continue
            if info.compress_type == zipfile.ZIP_STORED:
                page = 16384 if info.filename.endswith(".so") else 4
            else:
                page = 4
            data = info.header_offset + LOCAL_HEADER_LEN + len(info.filename.encode("utf-8")) \
                + len(info.extra)
            if data % page != 0:
                print("MISALIGNED %s data=%d mod%d=%d" % (info.filename, data, page, data % page))
                ok = False
    return ok


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "--check":
        sys.exit(0 if check(sys.argv[2]) else 1)
    if len(sys.argv) != 3:
        print("usage: align_apk.py [--check] in.apk out.apk", file=sys.stderr)
        sys.exit(2)
    align(sys.argv[1], sys.argv[2])
    sys.exit(0 if check(sys.argv[2]) else 1)
