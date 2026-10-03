# Cat S22 Flip: hardware survey and Linux feasibility

Collected 2026-10-02 over unrooted ADB (`collect.sh`; raw data in `dump/`).
Firmware: `LTE_S02113.11_N_S22Flip_0.030.03` (V30), Android 11 (Go), kernel 4.9.227, security patch 2022-06-05.

## Platform

| Item | Finding | Source |
|---|---|---|
| SoC | **Qualcomm QM215** (soc_id 386, `8917A-PAASANAZA`). This is an MSM8917 derivative. | `props/soc0.txt` |
| CPU | 4× Cortex-A53 @ 1.2096 GHz. Stock userspace and kernel are **32-bit ARMv7**; the cores are ARMv8. | `proc/cpuinfo.txt` |
| GPU | Adreno 308 (`kgsl-3d0`) | bus devices |
| RAM | 2 GB (1929864 kB visible) | `proc/meminfo.txt` |
| Storage | eMMC on `sdhci@7824900` (mmc0), microSD on `sdhci@7864900` (mmc1). 56 GPT partitions, dynamic `super`. | `sys/mmc.txt`, by-name listing |
| PMIC | **PM8916** (`pm8916@0/@1` on SPMI). Linear charger (`qpnp-linear-charger`) and VM-BMS fuel gauge. | bus devices |
| Battery | DT profile `qcom,Q2805-batterydata` | DT paths |
| Board | QRD reference design, `dtb_idx=12`, `dtbo_idx=27` | getprop |
| Bootloader | Locked (verifiedbootstate green). **`ro.oem_unlock_supported=1`, `sys.oem_unlock_allowed=1`**, so `fastboot flashing unlock` should work. | getprop |

## Peripherals

| Function | Hardware | Bus / location | Mainline status |
|---|---|---|---|
| Main display | 480×640 @ 60 Hz, MIPI DSI (`mdss_dsi_ctrl0`). The exact panel is not yet known. DT candidates for a VGA panel: `gc9503v_jutai_vga_video`, `st7701s_vga_video`, `rouxian_st7701s_boe_video`, `jd9161z_boe_ips_video`. | DSI0 | MDSS/DSI are supported. The panel driver must be generated from the downstream DTB (e.g. with linux-mdss-dsi-panel-driver-generator). **Needs boot.img.** |
| Outer display | 128×128 @ 30 Hz, driven by MDSS SPI (`qcom,mdss_spi`). Its config comes from the stock dtbo overlay (entry 27), node `qcom,mdss_spi_st7789v2_qvga_cmd`. Despite the name, it is 128×128 and ST7735S-class. | `spi@7af6000` (spi6), CS1 | **Working** with `panel-mipi-dbi-spi`; see "Outer display" below. |
| Touch (main display) | Chipsemi CHSC (`chsc_cap_touch`, driver `semi_touch`) | i2c-3 (`i2c@78b7000`) addr **0x2e**. Unpopulated alternatives in DT: goodix@14, tsc@24, focaltech@38. | **No mainline driver.** Needs a port. |
| Keypad | `gpio-matrix-keypad`, plus `gpio-keys` (vol_up), PM8916 PON (power) and RESIN | GPIO | Mainline. Row/column GPIOs and keymap need the DTB. |
| Hall sensor (lid) | `hall_sensor` GPIO input | GPIO | Use `gpio-keys` with `SW_LID`. |
| Keypad backlight | `qcom,leds-gpio-keyboard_light` | GPIO | Use `gpio-leds`. |
| Camera flash | `qcom,leds-gpio-flash` + `qcom,camera-gpio-flash` | GPIO | Use `gpio-leds` or `sgm3140` (as on the Nokia 2780). |
| Vibrator | PM8916 vibrator @ c000 | SPMI | Mainline (`pm8xxx-vibrator`). |
| Audio codec | PM8916 analog codec (`analog-codec@f000`) + MSM digital codec, `msm8952-asoc-wcd` card "msm8952-snd-card-mtp", ADSP (Q6/APR) | SPMI + LPASS | Mainline (`msm8916-wcd-analog/digital`, qdsp6). |
| Speaker amplifier | **Awinic AW881xx smart PA** (driver bound) | i2c-5 (`i2c@7af5000`) addr **0x34** | Mainline has aw88395/aw88261/aw88399/aw88081. The exact AW881xx variant needs checking. |
| Speaker amplifier (alt) | Awinic AW8155 GPIO class-D amp (`soc:aw8155`) | GPIO | `simple-audio-amplifier` / `aw8738`-style pulse mode. |
| Headset | 3.5 mm jack with MBHC (Headset Jack + Button Jack inputs) | PM8916 codec | Mainline. |
| USB-C | **WillSemi WUSB3801** Type-C controller | i2c-5 addr **0x60** | Mainline (`wusb3801`). |
| USB-C audio switch | FSA4480 in DT, **not bound** (probably not populated) | i2c-5 addr 0x42 | Mainline (`fsa4480`). |
| USB | ChipIdea HS OTG (`78db000.usb`, `msm_otg`) | | Mainline (`ci_hdrc_msm`). |
| WiFi/BT/FM | WCNSS: Pronto core (`a21b000`) + iris RF chip (probably **WCN3615/3620**, as on other QM215 boards). BT over SMD, FM via `iris-fm`. | | Mainline (`wcn36xx`, `qcom_wcnss_iris`, `btqcomsmd`). Needs the device's signed `wcnss.mdt` and NV. |
| Modem | Q6v5 MSS (`4080000.qcom,mss`), BAM-DMUX data, MPSS `SDM439_GENNS_PACK` 3-00078. LTE/GSM/CDMA/IMS features. | | Mainline (`qcom_q6v5_mss`, `bam-dmux`, qrtr). ModemManager works on msm8916-family boards. VoLTE calls are hard; data and SMS are feasible. |
| GNSS | Via modem (`android.hardware.location.gps`) | | Feasible through the modem's QMI loc service. |
| NFC | `nqx` HAL configured and a `pinctrl/nfc` node exists, but **no NFC feature is reported and no NFC i2c device is bound**, so it is likely absent. | | n/a |
| Rear camera | **GalaxyCore GC5035** 5 MP + **DW9714** VCM + `gc5035_otp` EEPROM | CCI `camera@0`, `actuator@0`, CSIPHY | `gc5035` is in the msm89x7 tree (used by Nokia 2780). `dw9714` is mainline. CAMSS 8x17 is enabled in that tree. |
| Front camera | **GalaxyCore GC02M2** 2 MP | CCI `camera@1` | **No mainline driver.** Needs a port. |
| Accelerometer | Sensortek **STK8BA53** @ 0x18 | **BLSP i2c bus 4, owned by the ADSP sensor core (SSC)** | `stk8ba50` IIO driver (check compatibility). Linux control of the bus is untested. |
| Light/proximity | Sensortek **STK3x1x** @ 0x48 (alternate LTR55x @ 0x23) | same, ADSP | `stk3310` IIO driver |
| Pressure | InvenSense **ICP-10100** @ 0x63 (alternate SPL06 @ 0x77) | same, ADSP | `icp10100` IIO driver |
| Video codec | Venus (`1d00000.qcom,vidc`) | | Mainline (venus, msm8916-class) |
| Thermal | tsens @ 4a8000 | | Mainline |

## Linux feasibility: positive

- **QM215 + PM8916 is already a known mainline target.** The [msm89x7-mainline/linux](https://github.com/msm89x7-mainline/linux) tree (branch `msm89x7/7.1.3`) has `qm215.dtsi` and `qm215-pm8916.dtsi`. The closest relative is **`qm215-nokia-weeknd.dts` (Nokia 2780 Flip)**: another QM215 flip phone with the same GC5035 camera, gpio-matrix-keypad, PM8916 charger/BMS, WCN3620, modem, bam-dmux and CAMSS. Copies of those DTS files are in `reference/`.
- The bootloader is unlockable, and people on XDA have already rooted this device with a Magisk-patched V30 boot.img.
- The normal boot route is **lk2nd** (flashed to `boot`) chainloading a mainline arm64 kernel. Although stock is 32-bit, the A53 cores and the msm8916/8917 LK are generally able to boot aarch64.

### Work needed for this device

1. **Main DSI panel driver.** Generate it from the downstream DT panel node (init commands and timings).
2. **Outer 128×128 SPI display.** Configure `panel-mipi-dbi-spi` with the init sequence from DT/dtbo, and work out the CS/DC/reset GPIOs.
3. **CHSC touch driver.** No mainline driver exists. The touchscreen is a secondary input on a keypad phone, so this is lower priority.
4. **GC02M2 front camera driver.** Lower priority.
5. **AW881xx speaker amp.** Identify the exact part and check it against the mainline Awinic drivers.
6. **ADSP-owned sensors.** Either leave them on the ADSP (needs SSC/QMI userspace) or try driving BLSP1 i2c-4 from Linux.

## What could not be collected without root, and how to get it

SELinux (`u:r:shell:s0`, enforcing) blocks DT property values, `/proc/cmdline`, dmesg, `/proc/asound`, sysfs `uevent`/`name` files, and all firmware mounts. That leaves these gaps:

| Needed | Why | How |
|---|---|---|
| `boot.img` (kernel + appended DTBs; this board uses `dtb_idx` 12) and `dtbo.img` (`dtbo_idx` 27) | Panel init sequences, keypad matrix GPIOs and keymap, hall/LED/flash GPIOs, regulator mapping, SPI display config, panel selection | Option A: the V30 boot.img from the XDA root thread. Option B: after unlock and root, `dd` the `boot`, `dtbo`, `recovery` and `aboot` partitions. |
| `/proc/cmdline` | Shows which `mdss_mdp.panel=` is active | Root, or `fastboot getvar` / dmesg after root. |
| Firmware: `modem` partition (vfat, mounted at `/vendor/firmware_mnt`), `/vendor/firmware` (wcnss, venus, a300 GPU), `dsp` partition, `persist` (WiFi/BT MAC, sensor calibration), `modemst1/2`/`fsg` (EFS backup, **back these up before experimenting**) | The blobs are OEM-signed, so they have to come from this device or its firmware package. | Root, then `dd` the partitions or `tar` the mounts. |
| Full `dmesg` | Probe order, panel name, WCN chip ID, touch FW | Root |

## Resolved with root (2026-10-03)

Sources: `dump/root/` (`cmdline.txt`, `dmesg.txt`, `live.dts` = the live merged DT from `/sys/firmware/fdt`, `asound.txt`, `persist_files/`) and `dump/fs/vendor_root/` (`/vendor/firmware`).

| Item | Finding |
|---|---|
| Main panel | **Sitronix ST7701S**, `qcom,mdss_dsi_st7701s_vga_video` (cmdline `mdss_mdp.panel=1:dsi:0:...`). 480×640 at 60 Hz, video/burst mode, **2 DSI lanes**, 24 bpp, 43×58 mm. Porches: h 160/60/20, v 44/82/4. Reset sequence 1/120 ms, 0/120 ms, 1/120 ms. PWM backlight from PM8916 (max 4095). Full on/off command set is in `live.dts`, node `qcom,mdss_dsi_st7701s_vga_video` (line ~6624). |
| Outer panel | DT name "st7789v2 qvga", but the panel is really **128×128, RGB565, 27 fps**, with an **ST7735-style init** (B1–B4, C0–C5, E0/E1 gamma, `2A 00 02 00 81` / `2B 00 03 00 82`, so column/row offset 2/3). It sits on BLSP2 QUP2 SPI (spi6) at 50 MHz. GPIOs: reset **125**, D/C **68**, TE **124**, backlight **12** (pulse-count GPIO backlight). Mainline candidates: `panel-mipi-dbi-spi` with this init, or `st7735r`. |
| Keypad | `gpio-matrix-keypad`. Rows GPIO **126, 127, 45, 97, 62**; columns GPIO **96, 99, 90, 89, 129**; debounce 3 ms. Keymap: r0 = PHONE 3 6 9 #, r1 = LEFT 2 5 8 0, r2 = MICMUTE 1 4 7 *, r3 = ENTER RIGHT BACKSPACE UP DOWN, r4 = BACK APPSELECT HOMEPAGE NUMERIC_B NUMERIC_A. Volume up = GPIO **91** (active low). Power and end keys = PM8916 PON. |
| Hall sensor | `hall-switch`, GPIO **44** (active low), wakeup |
| Keypad backlight | GPIO **130** |
| Speaker amp | **Awinic AW88194A** at i2c-5 0x34 (driver `aw881xx`). **No mainline driver**; mainline Awinic drivers cover aw88395/88261/88399/88081, not aw88194. Firmware/config files are `aw881xx_*.bin` in `dump/fs/vendor_root/vendor/firmware/`; calibration is in `persist/aw_cali.bin`. |
| FSA4480 | **Not fitted.** Probe fails with -107 (no device on the bus). |
| Touch | Chipsemi CHSC at 0x2e, vid_pid `0x06007306`, boot version `0x8020`, reports 480×640 |
| Cameras | `gc5035 probe succeeded`, `gc02m2 probe succeeded` |
| WiFi/BT | Pronto core in the SoC + **WCN3610** RF companion. dmesg `wcnss: IRIS Reg: 91100004` gives chip ID `0x9110`, which is `WCN3610V1` in downstream `wcnss_vreg.c`. 2.4 GHz-only b/g/n + BT. Pronto firmware 1.5.1.2 (`CNSS-PR-4-0-3-0000314`). NV file: `persist_files/WCNSS_qcom_wlan_nv.bin`. **Mainline:** no `qcom,wcn3610` compatible as of 7.3-rc5. Use the pending series "Add support for Qualcomm WCN3610" (v3, 2026-03, Kerigan Creighton; tested on Anki Vector), which adds the compatible to `qcom_wcnss_iris` and chip-specific config to `wcn36xx`. |
| GPU firmware | `a300_pfp.fw`, `a300_pm4.fw` in `/vendor/firmware` |
| Sound card | `msm8952-snd-card-mtp` |

### Backups (`backup/partitions/`)

All 55 partitions except `userdata` (and RPMB), plus the primary and backup GPT, 3.3 GB in total. Each image's SHA-1 was verified against the device (`SHA1SUMS`). `modem` and `dtbo` are byte-identical to the rebuilt OTA v30 images. Note that `boot.img` in this set is the **Magisk-patched** boot. The stock v30 boot is `ota/work/v30/images/boot.img`.
Script: `tools/backup_partitions.sh`.

## Bootloader notes (tested 2026-10-03)

- Bootloader unlocks with `fastboot flashing unlock` (LK accepted images afterwards).
- **`fastboot boot` does not work on stock LK** for this boot image (header v2, ARM zImage, 27 DTBs in the v2 dtb section):
  - As-is (Magisk-patched v30): `FAILED (remote: 'dtb not found')`. LK's `cmd_boot` path appears to look only for DTBs appended to the kernel, not the header-v2 dtb section.
  - With all 27 DTBs also appended to the zImage (`ota/magisk_patched_boot_v30_fastbootboot.img`): passes the DTB check, then `FAILED (remote: 'unknown reason')`. The cause is unknown without LK source or UART.
  - So testing means `fastboot flash boot`. The verified stock v30 `boot.img` (`ota/work/v30/images/boot.img`, SHA-1 `2781175354d624db73f6a172b796c2c563058e66`) is the rollback image.
- This matters later for lk2nd. Expect to flash it to `boot` rather than `fastboot boot` it.
- The OTAs in `ota/` provide the device trees and firmware without root. The v30 images were rebuilt from the v29 full OTA plus the v30 incremental using `tools/imgpatch.py`, and each matches the updater-script SHA-1.

## Booting lk2nd (working, 2026-10-03)

Stock LK is a newer Qualcomm VB2 aboot with "SoC DTB + DTBO" matching. lk2nd's upstream `qm215-mtp` entry did not boot on this unit. Getting it to boot took three things:

1. **No `qcom,board-id` in lk2nd's QM215 DTB.** Stock LK identifies the board as QRD v1.0, subtype 4 (fastboot `variant: QRD eMMC`, Android `platform_subtype_id 4`). It picks a DTB with only an msm-id (a "SoC DTB"), as its own QM215 DTB is. A board-id of MTP, or even the exact `<0x1000b 4>`, gave `dtb not found`.
2. **An empty `__symbols__ {}` node in that DTB.** LK always applies the matching board overlay from the `dtbo` partition, and libufdt's `ufdt_overlay_do_fixups` fails with "Bad main_symbols" without it.
3. **A minimal DTBO:** one entry with `qcom,board-id = <0x1000b 4>` and no fragments, flashed to `dtbo`. Otherwise the stock overlay patches nodes that lk2nd's DTB doesn't have.

Install from stock fastboot (enter it with Volume Down + Power from off):
```
fastboot flash dtbo dtbo-cat-s22flip.img
fastboot flash boot lk2nd.img
```
lk2nd then reports `lk2nd:model: Cat S22 Flip (S22FLIP)` and `lk2nd:panel: qcom,mdss_dsi_st7701s_vga_video`. Its own `fastboot boot` works for test kernels.

- Stock `fastboot boot` never works for lk2nd here. With the DTB fixes it gets past the DTB check, then `cmd_boot` fails silently ("unknown reason"). Padding the image to the AVB `boot` size (32 MiB) didn't help.
- To go back to Android, flash the stock `dtbo` and `boot` images.

## First mainline boot (2026-10-03)

Booted with `fastboot boot` through lk2nd: kernel `msm89x7/7.1.3` + WCN3610 v3 patches + `qm215-cat-s22flip.dts`, and a busybox initramfs that runs only in RAM.

| Area | Result |
|---|---|
| 4× A53, RAM, console on the main panel (`lk2nd.pass-simplefb`) | Works |
| USB gadget (NCM + ACM), telnet | Works once the PM8916 charger module provides extcon |
| Keypad, d-pad, soft/function keys, volume up/down, power, lid | All work. Programmable key = `KEY_NUMERIC_B`; `KEY_NUMERIC_A` (r4c4) is unidentified |
| Keypad debounce | Stock 3 ms gives double presses (domes chatter up to ~35 ms); 30 ms in our DTS |
| Charger (LBC) + BMS | Charging, capacity reported |
| WCNSS + WCN3610 | **WiFi works end to end:** scan (2.4 GHz only), WPA2-PSK/CCMP association on 802.11n, DHCP, DNS, NTP and HTTPS. Tested with `wpa_supplicant` from a RAM-only Alpine root. `hci0` registers too (Bluetooth not tested yet) |
| eMMC, microSD | Detected |
| WUSB3801 Type-C | First probe at boot fails silently (likely an I²C NACK early in boot); a manual rebind registers `port0` (sink/device, partner detected) |
| Outer display | Works: `panel-mipi-dbi` on SPI CS1, built-in driver, boot splash drawn by init (see below) |
| Modem, Venus | Need firmware the ramdisk doesn't carry (`modem.mbn` is 43 MB; lk2nd's ramdisk window is ~37 MB) |

lk2nd (msm8952) ignores boot header addresses: kernel at `0x80080000`, DTB at `0x83400000`, ramdisk at `0x83600000`. The ramdisk must end below the reserved region at `0x85b00000`.

## Outer display (working, 2026-10-03)

- **Controller:** ST7735S-class, 128×128 RGB565. Stock calls it "st7789v2 qvga", but the init sequence is ST7735S-style (B1–B4, C0–C5, E0/E1, FC).
- **Bus:** BLSP2 QUP2 SPI (`blsp2_spi2`), **chip select 1** (GPIO 22), 50 MHz. Only CS1 is muxed; CS0 (GPIO 47) is left alone. Data pins are GPIO 20/21/23.
- **Control lines:** D/C on GPIO 68, reset on GPIO 125, TE on GPIO 124 (unused).
- **Supplies:** L17 at 2.85 V and L6 at 1.8 V.
- **Backlight:** GPIO 12, as a plain `gpio-backlight`. Stock dims it with pulse counting. Stock's DT also lists GPIO 12 as the WUSB3801 reset; that's unverified.
- **Init sequence:** converted by `tools/mkmipidbi.py` from stock's `qcom,mdss-spi-on-command`, whose records are `delay_after_ms, len, cmd, params...`. The result is the `panel-mipi-dbi` firmware `cat,s22flip-ext-panel.bin`.
- **Window:** visible area starts at column 2, row 3, set via `hback-porch`/`vback-porch`. Stock's `MADCTL 0xCC` gives the correct colour order and an image that's upright with the lid closed.
- **Driver must be built in.** SPI devices only advertise `spi:s22flip-ext-panel`, which matches no module alias, so the module never auto-loads.
- **Nothing enables the pipeline by default** (fbcon only uses fb0). The bring-up init finds fb1 by name, unblanks it and draws `initramfs/splash/ext-splash.rgb565`.

## RAM-only Alpine toolbox (2026-10-03)

The busybox initramfs stays PID 1, keeping USB networking and the shell. An Alpine aarch64 root built by `tools/mkrootfs.sh` (minirootfs plus `iw`, `wpa_supplicant`, `wireless-regdb` and friends; about 11 MB compressed) is served over HTTP on the USB link. The phone streams it into a tmpfs:

```
mkdir -p /alpine && mount -t tmpfs -o size=512m tmpfs /alpine
wget -q -O - http://172.16.42.2:8022/alpine-s22.tar.gz | tar -xz -C /alpine
for m in proc sys dev dev/pts; do mount -o bind /$m /alpine/$m; done
cp /alpine/lib/firmware/regulatory.db* /lib/firmware/
```

Tools then run with `chroot /alpine`, using Alpine's `PATH` (`/usr/sbin:/usr/bin:/sbin:/bin`). Nothing is written to the eMMC or SD card.

Notes:
- Alpine's `wpa_supplicant` has no `-f`, and `wpa_passphrase` adds no control socket, so start it with `-C /run/wpa_supplicant`.
- The phone boots with its clock at 1970, so TLS fails until `ntpd -q -p pool.ntp.org` runs.

## Next steps

1. A root filesystem (e.g. postmarketOS) on the microSD card, for real userspace (`iw`, ModemManager) and to load the modem firmware.
2. ST7701S DSI panel driver: generate it from the `qcom,mdss_dsi_st7701s_vga_video` node of the live DT.
3. Outer display extras: pulse-count backlight dimming, and using it as a status or console display.
4. Fix the WUSB3801 first-probe failure.
5. Audio (PM8916 codec; AW88194A speaker amp needs a driver), cameras, touch.

## Contents of `dump/`

- `props/`: getprop, feature list, soc0 info
- `proc/`: cpuinfo, meminfo, interrupts, devices, etc. (some files contain "Permission denied")
- `sys/`: DT node tree (`devicetree_paths.txt`), every bus device with its bound driver (`bus_devices_uevents.txt`), drivers, input, regulators, thermal, power supply, cpufreq
- `dumpsys/`: display, input, sensors, camera, audio, telephony, wifi, lshal, getevent
- `logs/`: logcat (all buffers)
- `fs/vendor`: the readable 65 MB of /vendor (mixer paths, audio platform info, camera config and chromatix, sensor registry, WCNSS_qcom_cfg.ini, init rc, HALs, audio and WLAN kernel modules)
- `fs/system`, `fs/product`: keylayouts and etc
