#!/usr/bin/env python3
"""Extract the sensor registry group table from the phone's own sensors.qti.

The vendor sensor daemon stores the registry (sns.reg) as one flat image;
a table in the daemon gives each group's size, offset and ID as 16-bit
little-endian triples. This finds that table (the longest run of plausible
triples with increasing offsets) and prints "id size offset" lines for
sns-reg-serve.

usage: sns-reg-groups.py SENSORS_QTI SNS_REG > groups.txt
"""
import struct
import sys


def runs(data, reglen):
    """Yield (start, entries) for runs of (size, offset, id) triples."""
    for phase in range(6):
        o = phase
        cur, start, last = [], phase, -1
        while o + 6 <= len(data):
            size, off, gid = struct.unpack_from('<HHH', data, o)
            if 0 < size <= 0x400 and off + size <= reglen and off > last:
                if not cur:
                    start = o
                cur.append((gid, size, off))
                last = off
            else:
                if cur:
                    yield start, cur
                cur, last = [], -1
            o += 6
        if cur:
            yield start, cur


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    data = open(sys.argv[1], 'rb').read()
    reglen = len(open(sys.argv[2], 'rb').read())
    start, best = max(runs(data, reglen), key=lambda r: len(r[1]))
    if len(best) < 20:
        sys.exit('no registry group table found')
    print(f'# {len(best)} groups from table at 0x{start:x}', file=sys.stderr)
    for gid, size, off in best:
        print(f'{gid} {size} 0x{off:x}')


if __name__ == '__main__':
    main()
