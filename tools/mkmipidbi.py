#!/usr/bin/env python3
"""Convert a downstream qcom,mdss-spi-on-command into a panel-mipi-dbi firmware file.

Downstream (mdss_spi_panel) records are: delay_after_ms, length, command, params...
panel-mipi-dbi wants: magic, version 1, then command, num_params, params...
with "0x00 0x01 <ms>" (NOP + 1 param) as a delay.

usage: mkmipidbi.py LIVE_DTB PANEL_NODE_PATH OUT.bin
"""
import subprocess
import sys

MAGIC = b"MIPI DBI" + bytes(7)
SKIP = {0x2C}  # RAMWR: the driver writes pixel data itself


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    dtb, node, out = sys.argv[1:]
    raw = subprocess.run(["fdtget", "-t", "bx", dtb, node, "qcom,mdss-spi-on-command"],
                         check=True, capture_output=True, text=True).stdout.split()
    data = [int(b, 16) for b in raw]

    cmds = bytearray()
    i = 0
    while i < len(data):
        delay, length = data[i], data[i + 1]
        payload = data[i + 2:i + 2 + length]
        if len(payload) != length or length == 0:
            sys.exit(f"malformed record at byte {i}: {data[i:i + 2 + length]}")
        i += 2 + length
        cmd, params = payload[0], payload[1:]
        note = " (skipped)" if cmd in SKIP else ""
        print(f"cmd 0x{cmd:02x} params [{' '.join(f'{p:02x}' for p in params)}] then {delay} ms{note}")
        if cmd in SKIP:
            continue
        cmds += bytes([cmd, len(params), *params])
        if delay:
            cmds += bytes([0x00, 0x01, delay])

    with open(out, "wb") as f:
        f.write(MAGIC + bytes([1]) + cmds)
    print(f"wrote {out}: {16 + len(cmds)} bytes, consumed {i}/{len(data)} downstream bytes")


if __name__ == "__main__":
    main()
