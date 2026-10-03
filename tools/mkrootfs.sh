#!/usr/bin/env bash
# Build the Alpine aarch64 toolbox root that the bring-up initramfs fetches over USB.
# Needs no root: packages are installed in an unprivileged user-namespace chroot,
# with qemu-user binfmt running the aarch64 binaries.
#
# usage: mkrootfs.sh OUT_DIR [extra packages...]
set -euo pipefail
OUT=${1:?usage: mkrootfs.sh OUT_DIR [packages...]}
shift
PKGS="iw wpa_supplicant wireless-regdb iproute2 evtest i2c-tools $*"
BASE=https://dl-cdn.alpinelinux.org/alpine/latest-stable/releases/aarch64

mkdir -p "$OUT"
cd "$OUT"
meta=$(curl -sfL $BASE/latest-releases.yaml | grep -A12 "flavor: alpine-minirootfs")
file=$(echo "$meta" | awk '/file:/{print $2; exit}')
sum=$(echo "$meta" | awk '/sha256:/{print $2; exit}')
[ -f "$file" ] || curl -sfLO "$BASE/$file"
echo "$sum  $file" | sha256sum -c -

[ -e alpine ] && { echo "$OUT/alpine exists; move it aside first" >&2; exit 1; }
mkdir alpine
tar -xzf "$file" -C alpine
cp /etc/resolv.conf alpine/etc/resolv.conf
unshare -r chroot alpine /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root \
	/bin/sh -c "apk update -q && apk add -q $PKGS"
unshare -r tar -C alpine -czf alpine-s22.tar.gz .
ls -l alpine-s22.tar.gz
