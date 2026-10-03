#!/usr/bin/env python3
"""Apply an Android OTA IMGDIFF2 patch (boot.img.p etc.) to a source image.

Re-implements bootable/recovery applypatch/imgpatch.cpp + bspatch so the
v29 -> v30 incremental OTA can be applied off-device.

usage: imgpatch.py SOURCE PATCH OUTPUT [EXPECTED_SHA1]
"""
import bz2
import hashlib
import struct
import sys
import zlib

import numpy as np

CHUNK_NORMAL, CHUNK_GZIP, CHUNK_DEFLATE, CHUNK_RAW = 0, 1, 2, 3


def offtin(b, off):
    # bsdiff stores int64 as sign-magnitude little-endian
    v = struct.unpack_from("<Q", b, off)[0]
    return -(v & 0x7FFFFFFFFFFFFFFF) if v & (1 << 63) else v


def bspatch(old, patch, off):
    if patch[off:off + 8] != b"BSDIFF40":
        raise ValueError(f"bad bsdiff magic at {off}: {patch[off:off + 8]!r}")
    ctrl_len = offtin(patch, off + 8)
    diff_len = offtin(patch, off + 16)
    new_size = offtin(patch, off + 24)
    p = off + 32
    ctrl = bz2.decompress(patch[p:p + ctrl_len])
    diff = bz2.decompress(patch[p + ctrl_len:p + ctrl_len + diff_len])
    # The extra block runs to an implicit end; the decompressor stops at end of stream.
    extra = bz2.BZ2Decompressor().decompress(patch[p + ctrl_len + diff_len:])

    old_a = np.frombuffer(old, dtype=np.uint8)
    diff_a = np.frombuffer(diff, dtype=np.uint8)
    new = np.zeros(new_size, dtype=np.uint8)
    newpos = oldpos = dpos = epos = cpos = 0
    while newpos < new_size:
        x, y, z = (offtin(ctrl, cpos + 8 * i) for i in range(3))
        cpos += 24
        if newpos + x > new_size:
            raise ValueError("corrupt patch (diff overruns output)")
        seg = diff_a[dpos:dpos + x].copy()
        # bytes outside the old image contribute 0
        lo, hi = max(oldpos, 0), min(oldpos + x, len(old_a))
        if hi > lo:
            seg[lo - oldpos:hi - oldpos] += old_a[lo:hi]
        new[newpos:newpos + x] = seg
        newpos += x
        oldpos += x
        dpos += x
        if newpos + y > new_size:
            raise ValueError("corrupt patch (extra overruns output)")
        new[newpos:newpos + y] = np.frombuffer(extra, dtype=np.uint8, count=y, offset=epos)
        newpos += y
        epos += y
        oldpos += z
    return new.tobytes()


def imgpatch(src, patch):
    if patch[:8] != b"IMGDIFF2":
        raise ValueError(f"not an IMGDIFF2 patch: {patch[:8]!r}")
    (num_chunks,) = struct.unpack_from("<i", patch, 8)
    pos = 12
    out = bytearray()
    for i in range(num_chunks):
        (ctype,) = struct.unpack_from("<i", patch, pos)
        pos += 4
        if ctype == CHUNK_NORMAL:
            src_start, src_len, poff = struct.unpack_from("<QQQ", patch, pos)
            pos += 24
            out += bspatch(src[src_start:src_start + src_len], patch, poff)
        elif ctype == CHUNK_RAW:
            (n,) = struct.unpack_from("<i", patch, pos)
            pos += 4
            out += patch[pos:pos + n]
            pos += n
        elif ctype == CHUNK_DEFLATE:
            src_start, src_len, poff, expanded_len, target_len = struct.unpack_from("<QQQQQ", patch, pos)
            level, method, wbits, memlevel, strategy = struct.unpack_from("<iiiii", patch, pos + 40)
            pos += 60
            d = zlib.decompressobj(-15)
            expanded = d.decompress(src[src_start:src_start + src_len])
            if len(expanded) != expanded_len:
                raise ValueError(f"chunk {i}: inflated {len(expanded)} != {expanded_len}")
            target = bspatch(expanded, patch, poff)
            if len(target) != target_len:
                raise ValueError(f"chunk {i}: patched {len(target)} != {target_len}")
            c = zlib.compressobj(level, method, wbits, memlevel, strategy)
            out += c.compress(target) + c.flush()
        else:
            raise ValueError(f"chunk {i}: unsupported type {ctype}")
    return bytes(out)


def main():
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    src = open(sys.argv[1], "rb").read()
    patch = open(sys.argv[2], "rb").read()
    out = imgpatch(src, patch)
    sha1 = hashlib.sha1(out).hexdigest()
    open(sys.argv[3], "wb").write(out)
    print(f"{sys.argv[3]}: {len(out)} bytes sha1 {sha1}")
    if len(sys.argv) == 5:
        if sha1 != sys.argv[4]:
            sys.exit(f"SHA1 MISMATCH: expected {sys.argv[4]}")
        print("  sha1 matches OTA target")


if __name__ == "__main__":
    main()
