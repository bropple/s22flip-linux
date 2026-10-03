#!/usr/bin/env bash
# Render the 128x128 outer-display splash as raw little-endian RGB565.
set -euo pipefail
cd "$(dirname "$0")"
FONT=/usr/share/fonts/misc
magick -size 128x128 xc:black \
	-font $FONT/ter-u22b.otb -fill '#ffcc00' -gravity north -pointsize 22 -annotate +0+18 'Cat S22' \
	-font $FONT/ter-u22b.otb -fill white -gravity center -pointsize 22 -annotate +0+4 'Linux' \
	-font $FONT/ter-u20n.otb -fill '#9a9a9a' -gravity south -pointsize 20 -annotate +0+16 'mainline' \
	-depth 8 rgb:ext-splash.rgb
python3 - <<'EOF'
import struct
d = open("ext-splash.rgb", "rb").read()
px = [((d[i] >> 3) << 11) | ((d[i + 1] >> 2) << 5) | (d[i + 2] >> 3) for i in range(0, len(d), 3)]
open("ext-splash.rgb565", "wb").write(struct.pack("<%dH" % len(px), *px))
EOF
