#!/bin/sh
# Start rmtfs on RAM copies of the EFS (read-only: never writes storage), then boot the modem.
killall httpd 2>/dev/null
A="chroot /alpine /usr/bin/env PATH=/usr/sbin:/usr/bin:/sbin:/bin"

if ! pidof rmtfs >/dev/null; then
	$A sh -c 'rmtfs -o /var/lib/efs-ram -r -v > /tmp/rmtfs.log 2>&1 &'
	sleep 2
fi
echo "rmtfs pid: $(pidof rmtfs)"

for r in /sys/class/remoteproc/remoteproc*; do
	[ "$(cat $r/name)" = 4080000.remoteproc ] && R=$r
done
echo "modem rproc: $R state=$(cat $R/state)"
[ "$(cat $R/state)" = offline ] && echo start > $R/state
for i in $(seq 1 30); do
	[ "$(cat $R/state)" = running ] && break
	sleep 1
done
echo "modem state: $(cat $R/state)"
echo "--- dmesg"
dmesg | grep -iE "remoteproc|mss|modem|q6v5|rmtfs|sysmon|qrtr" | tail -15
echo "--- rmtfs log"
tail -15 /alpine/tmp/rmtfs.log
