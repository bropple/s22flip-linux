#!/usr/bin/env bash
# Build a RAM-only reference boot image: the stock v30 kernel and the device
# tree captured from stock (overlays merged), with an Alpine armv7 root in
# place of Android's ramdisk. Used to observe stock drivers (e.g. aw881xx)
# on live hardware without booting Android or touching the eMMC.
#
# Needs qemu-arm binfmt for the unprivileged chroot. Output: out/stockref/
#
# Options (environment):
#   STOCKREF_WLAN_TRACE=1  turn up prima's control-path traces (WDA, WDI, PE,
#                          SME) in the image's copy of WCNSS_qcom_cfg.ini
#   STOCKREF_MODEM=1       also install the modem firmware (mba, modem.*);
#                          `cat /dev/subsys_modem` then boots it (no EFS)
#   STOCKREF_CRASHDUMP=1   on a panic, enter Qualcomm crash-dump mode
#                          (05c6:900e) instead of rebooting, for a RAM dump
set -euo pipefail
TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=$TOP/out/stockref
ROOT=$OUT/root
BASE=https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/armv7
PKGS="alsa-utils busybox-extras i2c-tools libgpiod wpa_supplicant iw"

mkdir -p "$OUT"
[ -e "$ROOT" ] && { echo "$ROOT exists; move it aside first" >&2; exit 1; }

cd "$OUT"
meta=$(curl -sfL $BASE/latest-releases.yaml | grep -A12 "flavor: alpine-minirootfs")
file=$(echo "$meta" | awk '/file:/{print $2; exit}')
sum=$(echo "$meta" | awk '/sha256:/{print $2; exit}')
[ -f "$file" ] || curl -sfLO "$BASE/$file"
echo "$sum  $file" | sha256sum -c -

mkdir "$ROOT"
tar -xzf "$file" -C "$ROOT"
cp /etc/resolv.conf "$ROOT/etc/resolv.conf"
unshare -r chroot "$ROOT" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root \
	/bin/sh -c "apk update -q && apk add -q $PKGS"

install -m 755 "$TOP/initramfs/stockref-init" "$ROOT/init"

# Stock firmware: split ADSP image (the downstream PIL loads adsp.mdt) and
# the AW881xx amplifier profile; the stock kernel searches /lib/firmware
mkdir -p "$ROOT/lib/firmware"
cp "$TOP/firmware/src/modem"/adsp.* "$ROOT/lib/firmware/"
cp "$TOP/dump/fs/vendor_root/vendor/firmware/aw881xx_acf.bin" "$ROOT/lib/firmware/"
# Stock WiFi (wifi-up): the split WCNSS image for the downstream PIL, prima's
# config and calibration where it requests them, and the prima module
cp "$TOP/firmware/src/modem"/wcnss.* "$ROOT/lib/firmware/"
[ -n "${STOCKREF_MODEM:-}" ] &&
	cp "$TOP/firmware/src/modem"/mba.mbn "$TOP/firmware/src/modem"/modem.* "$ROOT/lib/firmware/"
mkdir -p "$ROOT/lib/firmware/wlan/prima" "$ROOT/vendor/lib/modules"
cp "$TOP/dump/fs/vendor/etc/wifi/WCNSS_qcom_cfg.ini" \
	"$TOP/dump/root/persist_files/WCNSS_qcom_wlan_nv.bin" \
	"$TOP/dump/root/persist_files/WCNSS_wlan_dictionary.dat" "$ROOT/lib/firmware/wlan/prima/"
cp "$OUT/vendor-modules/modules/pronto_wlan.ko" "$ROOT/vendor/lib/modules/"
if [ -n "${STOCKREF_WLAN_TRACE:-}" ]; then
	# Bitmasks over trace levels (bit 0 FATAL .. bit 7 DEBUG); the parser
	# stops at END, so the keys go before it. Data path (TL, DAT) left alone,
	# and WDI CTL without WARN (it logs every TX frame). gMulticastHostMsgs=0
	# sends the traces to kmsg instead of an (absent) Android log daemon.
	# WDA's INFO level includes a hex dump of the whole START config.
	sed -i '/^END$/i \
vosTraceEnableWDA=255\
vosTraceEnableWDI=255\
vosTraceEnablePE=15\
vosTraceEnableSME=15\
vosTraceEnableHDD=7\
wdiTraceEnableDAL=127\
wdiTraceEnableCTL=123\
gMulticastHostMsgs=0\
' "$ROOT/lib/firmware/wlan/prima/WCNSS_qcom_cfg.ini"
	grep -q '^vosTraceEnableWDA=255$' "$ROOT/lib/firmware/wlan/prima/WCNSS_qcom_cfg.ini"
fi
install -m 755 "$TOP/initramfs/stockref-wifi-up" "$ROOT/usr/bin/wifi-up"

(cd "$ROOT" && find . | cpio -o -H newc -R 0:0 --quiet | gzip -9) > "$OUT/ramdisk.cpio.gz"

STOCK=$OUT/stock-unpacked
mkdir -p "$STOCK"
unpack_bootimg --boot_img "$TOP/ota/work/v30/images/boot.img" --out "$STOCK" >/dev/null

# The DTB stock LK would build: the QM215 SoC DTB (entry 12 of the boot
# image's DTB section) with the board overlay (entry 27 of dtbo.img)
python3 - "$STOCK/dtb" "$TOP/ota/work/v30/images/dtbo.img" "$OUT" <<'PY'
import struct, sys
dtbs, dtbo, out = sys.argv[1:]
d = open(dtbs, 'rb').read()
i = n = 0
while i < len(d) - 8:
    if d[i:i + 4] == b'\xd0\x0d\xfe\xed':
        size = struct.unpack_from('>I', d, i + 4)[0]
        if n == 12:
            open(f'{out}/stock-dtb12.dtb', 'wb').write(d[i:i + size])
        n += 1
        i += size
    else:
        i += 4
t = open(dtbo, 'rb').read()
magic, _, _, esz, _, off = struct.unpack_from('>6I', t, 0)
assert magic == 0xd7b7ab1e
size, eoff = struct.unpack_from('>2I', t, off + 27 * esz)
open(f'{out}/stock-dtbo27.dtbo', 'wb').write(t[eoff:eoff + size])
PY
fdtoverlay -i "$OUT/stock-dtb12.dtb" -o "$OUT/stock-merged.dtb" "$OUT/stock-dtbo27.dtbo"

# lk2nd only finds the DTB when it is appended to the zImage (header v0).
# On a panic, reboot (back into lk2nd) instead of entering crash-dump mode,
# unless STOCKREF_CRASHDUMP asks for crash-dump mode.
cat "$STOCK/kernel" "$OUT/stock-merged.dtb" > "$OUT/zImage-dtb"
dload=0
[ -n "${STOCKREF_CRASHDUMP:-}" ] && dload=1
mkbootimg --header_version 0 --pagesize 2048 \
	--kernel "$OUT/zImage-dtb" --ramdisk "$OUT/ramdisk.cpio.gz" \
	--cmdline "console=ttyMSM0,115200,n8 androidboot.hardware=qcom user_debug=30 msm_rtb.filter=0x237 ehci-hcd.park=3 lpm_levels.sleep_disabled=1 vmalloc=300M androidboot.usbconfigfs=true loop.max_part=7 rdinit=/init panic=3 msm_poweroff.download_mode=$dload subsystem_restart.enable_ramdumps=0" \
	-o "$OUT/stockref.img"

ls -l "$OUT/ramdisk.cpio.gz" "$OUT/stockref.img"
