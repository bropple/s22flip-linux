#!/usr/bin/env bash
# Stream every eMMC partition (except userdata) off a rooted phone and verify SHA-1 on both ends.
set -u
OUT="${1:-$(dirname "$0")/../backup/partitions}"
SKIP="userdata mmcblk0 mmcblk0rpmb"
mkdir -p "$OUT"
cd "$OUT" || exit 1
su() { adb shell "su -c '$1'" < /dev/null; }

su 'cd /dev/block/by-name && for p in *; do d=$(readlink -f $p); echo "$p ${d##*/}"; done' | tr -d '\r' > partmap.txt

# Primary and backup GPT (512-byte sectors: first 34, last 33)
DISK_SECTORS=$(su 'blockdev --getsz /dev/block/mmcblk0' | tr -d '\r')
adb exec-out "su -c 'dd if=/dev/block/mmcblk0 bs=512 count=34 2>/dev/null'" > gpt_primary.bin
adb exec-out "su -c 'dd if=/dev/block/mmcblk0 bs=512 skip=$((DISK_SECTORS - 33)) count=33 2>/dev/null'" > gpt_backup.bin

: > SHA1SUMS
fail=0
while read -r name dev; do
  case " $SKIP " in *" $name "*) continue ;; esac
  adb exec-out "su -c 'dd if=/dev/block/$dev bs=1048576 2>/dev/null'" < /dev/null > "$name.img"
  remote=$(su "sha1sum /dev/block/$dev" | awk '{print $1}')
  local_=$(sha1sum "$name.img" | awk '{print $1}')
  if [ "$remote" = "$local_" ]; then status=OK; else status=MISMATCH; fail=1; fi
  printf "%-18s %-12s %10s  %s\n" "$name" "$dev" "$(stat -c %s "$name.img")" "$status"
  echo "$local_  $name.img" >> SHA1SUMS
done < partmap.txt
sha1sum gpt_primary.bin gpt_backup.bin >> SHA1SUMS
exit $fail
