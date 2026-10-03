#!/usr/bin/env bash
# Collect hardware info from a Cat S22 Flip over (unrooted) ADB.
set -u
OUT="${1:-$(dirname "$0")/dump}"
mkdir -p "$OUT"/{props,proc,sys,dumpsys,logs,fs}
A="adb shell"

echo "[*] properties"
$A getprop > "$OUT/props/getprop.txt"
$A pm list features > "$OUT/props/features.txt"
$A 'cat /sys/devices/soc0/* 2>/dev/null; for f in /sys/devices/soc0/*; do echo "$f: $(cat $f 2>&1)"; done' > "$OUT/props/soc0.txt"

echo "[*] /proc"
for f in cpuinfo meminfo version devices misc interrupts modules partitions iomem filesystems mounts \
         bus/input/devices bus/input/handlers crypto zoneinfo swaps cmdline consoles execdomains vmallocinfo \
         pagetypeinfo buddyinfo softirqs stat; do
  $A "cat /proc/$f" > "$OUT/proc/$(echo $f | tr / _).txt" 2>&1
done

echo "[*] devicetree node structure (names only; properties are SELinux-protected)"
$A 'find /sys/firmware/devicetree/base 2>/dev/null' | sed 's|/sys/firmware/devicetree/base||' > "$OUT/sys/devicetree_paths.txt"

echo "[*] platform/i2c/spi/etc device uevents (gives OF compatibles + drivers)"
$A 'for bus in /sys/bus/*; do
      for d in $bus/devices/*; do
        echo "=== $d"
        echo "driver: $(readlink $d/driver 2>/dev/null | sed "s|.*/||")"
        cat $d/uevent 2>/dev/null
        [ -r $d/name ] && echo "name: $(cat $d/name 2>/dev/null)"
        [ -r $d/modalias ] && echo "modalias: $(cat $d/modalias 2>/dev/null)"
      done
    done' > "$OUT/sys/bus_devices_uevents.txt"
$A 'for d in /sys/bus/*/drivers/*; do echo "$d -> $(ls $d 2>/dev/null | grep -vE "^(bind|unbind|uevent|module|new_id|remove_id)$" | tr "\n" " ")"; done' > "$OUT/sys/drivers_bound.txt"
$A 'ls -l /sys/class/*/ 2>/dev/null' > "$OUT/sys/classes.txt"
$A 'ls -l /sys/dev/block /sys/dev/char 2>/dev/null' > "$OUT/sys/dev_nodes.txt"
$A 'ls -lR /dev 2>/dev/null' > "$OUT/sys/dev_listing.txt"
$A 'ls /sys/module' > "$OUT/sys/modules_builtin_and_loaded.txt"
$A 'for m in /sys/module/*/parameters; do for p in $m/*; do echo "$p = $(cat $p 2>/dev/null)"; done; done' > "$OUT/sys/module_params.txt"

echo "[*] class details: input / display / power / thermal / leds / backlight / net / sound / video / regulators"
$A 'for d in /sys/class/input/input*; do echo "=== $d"; for f in name phys uniq properties modalias; do echo "$f: $(cat $d/$f 2>/dev/null)"; done; for f in $d/capabilities/*; do echo "cap $(basename $f): $(cat $f 2>/dev/null)"; done; echo "devpath: $(readlink -f $d)"; done' > "$OUT/sys/input.txt"
$A 'for d in /sys/class/graphics/fb* /sys/class/drm/* /sys/class/backlight/* /sys/class/leds/*; do echo "=== $d -> $(readlink -f $d)"; for f in $d/*; do [ -f $f ] && [ -r $f ] && echo "$(basename $f): $(head -c 2000 $f 2>/dev/null | tr "\0" " ")"; done; done' > "$OUT/sys/display_backlight_leds.txt" 2>&1
$A 'for d in /sys/class/power_supply/*; do echo "=== $d -> $(readlink -f $d)"; cat $d/uevent 2>/dev/null; done' > "$OUT/sys/power_supply.txt"
$A 'for d in /sys/class/thermal/*; do echo "=== $d"; cat $d/type $d/temp 2>/dev/null; done' > "$OUT/sys/thermal.txt"
$A 'for d in /sys/class/regulator/*; do echo "=== $d -> $(readlink -f $d)"; for f in name type microvolts min_microvolts max_microvolts state num_users; do echo "$f: $(cat $d/$f 2>/dev/null)"; done; done' > "$OUT/sys/regulators.txt"
$A 'for d in /sys/class/net/*; do echo "=== $d -> $(readlink -f $d)"; cat $d/uevent $d/address 2>/dev/null; done' > "$OUT/sys/net.txt"
$A 'for d in /sys/class/sound/* /sys/class/video4linux/* /sys/class/misc/* /sys/class/mmc_host/* /sys/class/rfkill/* /sys/class/bluetooth/* /sys/class/extcon/* /sys/class/typec/* /sys/class/udc/* /sys/class/remoteproc/* /sys/class/subsys/* /sys/class/iio/devices/* /sys/class/sensors/* /sys/class/kgsl/*; do echo "=== $d -> $(readlink -f $d)"; cat $d/uevent $d/name 2>/dev/null; done' > "$OUT/sys/misc_classes.txt" 2>&1
$A 'for d in /sys/bus/mmc/devices/*; do echo "=== $d"; for f in $d/*; do [ -f $f ] && echo "$(basename $f): $(cat $f 2>/dev/null)"; done; done' > "$OUT/sys/mmc.txt"
$A 'cat /sys/kernel/debug/gpio /sys/kernel/debug/pinctrl/*/pinmux-pins 2>&1; ls -l /sys/class/gpio' > "$OUT/sys/gpio.txt" 2>&1
$A 'for d in /sys/devices/system/cpu/cpu*/cpufreq; do echo "=== $d"; cat $d/scaling_available_frequencies $d/cpuinfo_max_freq $d/scaling_driver 2>/dev/null; done; cat /sys/class/kgsl/kgsl-3d0/gpu_model /sys/class/kgsl/kgsl-3d0/gpu_available_frequencies /sys/class/kgsl/kgsl-3d0/devfreq/available_frequencies 2>/dev/null' > "$OUT/sys/cpu_gpu_freq.txt" 2>&1

echo "[*] dumpsys"
for s in SurfaceFlinger display input input_method audio media.audio_policy media.audio_flinger media.camera \
         wifi telephony.registry phone iphonesubinfo isub carrier_config connectivity bluetooth_manager battery \
         batterystats sensorservice nfc vibrator vibrator_manager usb hardware_properties thermalservice power \
         lights window; do
  timeout 30 $A dumpsys $s > "$OUT/dumpsys/$s.txt" 2>&1
done
$A 'lshal 2>&1' > "$OUT/dumpsys/lshal.txt"
$A 'service list' > "$OUT/dumpsys/service_list.txt"
$A 'getevent -lp' > "$OUT/dumpsys/getevent_lp.txt" 2>&1
$A 'ps -A -o PID,USER,NAME,ARGS' > "$OUT/dumpsys/processes.txt"

echo "[*] logs"
$A 'dmesg' > "$OUT/logs/dmesg.txt" 2>&1
$A 'logcat -b all -d' > "$OUT/logs/logcat_all.txt" 2>&1
$A 'logcat -b kernel -d' > "$OUT/logs/logcat_kernel.txt" 2>&1

echo "[*] filesystem pulls"
# tar skips files SELinux denies instead of aborting like adb pull does on /vendor
( cd "$OUT/fs" && for p in vendor odm; do adb exec-out "tar -cf - /$p 2>/dev/null" | tar -xf -; done )
for p in /system/etc /system/usr /product/etc /product/build.prop /system/build.prop; do
  dst="$OUT/fs$(dirname $p)"; mkdir -p "$dst"
  adb pull -a "$p" "$dst/" > /dev/null 2> "$OUT/logs/pull_$(echo $p | tr / _).err"
done
$A 'ls -lRZ /vendor /odm' > "$OUT/fs/vendor_odm_listing_with_selinux.txt" 2>&1

echo "[*] done -> $OUT"
