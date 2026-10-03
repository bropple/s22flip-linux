#!/bin/sh
# Copy the modem EFS partitions into RAM for "rmtfs -o DIR -r".
# Only reads the eMMC. Args: expected SHA-1s of modemst1 modemst2 fsc fsg (from the backup).
D=/alpine/var/lib/efs-ram
mkdir -p $D
set -- "modemst1 modem_fs1 $1" "modemst2 modem_fs2 $2" "fsc modem_fsc $3" "fsg modem_fsg $4"
for entry in "$@"; do
	set -- $entry
	part=$1 name=$2 want=$3
	uevent=$(grep -l "^PARTNAME=$part\$" /sys/class/block/*/uevent | head -n1)
	dev=$(echo "$uevent" | cut -d/ -f5)
	dd if=/dev/$dev of=$D/$name bs=4096 2>/dev/null
	got=$(sha1sum $D/$name | cut -d' ' -f1)
	[ "$got" = "$want" ] && r=match || r=MISMATCH
	echo "$part (/dev/$dev) -> $name: $(wc -c < $D/$name) bytes, backup hash $r"
done
