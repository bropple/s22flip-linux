#!/usr/bin/env bash
# Build the bring-up initramfs and an Android boot image for `fastboot boot` via lk2nd.
set -euo pipefail
TOP=$(cd "$(dirname "$0")/.." && pwd)
OUT=$TOP/out
ROOT=$OUT/initramfs-root
KREL=$(cat "$TOP/build/include/config/kernel.release")

mkdir -p "$OUT"
[ -e "$ROOT" ] && { echo "$ROOT exists; move it aside first" >&2; exit 1; }
mkdir -p "$ROOT"/{bin,etc/modprobe.d,lib/firmware}

install -m 755 "$TOP/initramfs/init" "$ROOT/init"
install -m 755 "$TOP/tools/busybox-1.37.0/busybox" "$ROOT/bin/busybox"

# Modules: busybox modprobe can't read .ko.zst, so decompress them
cp -a "$TOP/stage/lib/modules" "$ROOT/lib/"
find "$ROOT/lib/modules" -name '*.ko.zst' -exec zstd -q -d --rm {} \;
depmod -b "$ROOT" "$KREL"

# GPU microcode for the Adreno 308 (msm DRM drives both the GPU and the panel)
mkdir -p "$ROOT/lib/firmware/qcom"
cp "$TOP/firmware/lib/firmware/qcom"/a300_{pm4,pfp}.fw "$ROOT/lib/firmware/qcom/"

# WiFi firmware only; modem.mbn (43 MB) does not fit lk2nd's ramdisk window
FW=qcom/qm215/cat/s22flip
mkdir -p "$ROOT/lib/firmware/$FW"
cp "$TOP/firmware/lib/firmware/$FW"/{wcnss.mbn,WCNSS_qcom_wlan_nv.bin} "$ROOT/lib/firmware/$FW/"
# Outer display init sequence (tools/mkmipidbi.py from the stock DT)
cp "$TOP/firmware/lib/firmware/cat,s22flip-ext-panel.bin" "$ROOT/lib/firmware/"
mkdir -p "$ROOT/usr/share/s22"
cp "$TOP/initramfs/splash/ext-splash.rgb565" "$ROOT/usr/share/s22/"

(cd "$ROOT" && find . | cpio -o -H newc --quiet | gzip -9) > "$OUT/initramfs.cpio.gz"

DTB=$TOP/build/arch/arm64/boot/dts/qcom/qm215-cat-s22flip.dtb
cat "$TOP/build/arch/arm64/boot/Image.gz" "$DTB" > "$OUT/Image.gz-dtb"

# lk2nd (msm8952) ignores the header load addresses and uses fixed ones
mkbootimg --header_version 0 --pagesize 2048 \
	--kernel "$OUT/Image.gz-dtb" --ramdisk "$OUT/initramfs.cpio.gz" \
	--cmdline "console=tty0 loglevel=7 lk2nd.pass-simplefb clk_ignore_unused pd_ignore_unused" \
	-o "$OUT/s22flip-bringup.img"

ls -l "$OUT/initramfs.cpio.gz" "$OUT/Image.gz-dtb" "$OUT/s22flip-bringup.img"
