# Cat S22 Flip: hardware survey and Linux feasibility

Collected 2026-10-02 over unrooted ADB (`collect.sh`; raw data in `dump/`).
Firmware: `LTE_S02113.11_N_S22Flip_0.030.03` (V30), Android 11 (Go), kernel 4.9.227, security patch 2022-06-05.

## Status (2026-10-08, development paused)

Mainline Linux runs the phone as a daily device. Working: both displays and
the GPU, the touchscreen, the keypad and lid, WiFi and Bluetooth, the
earpiece, microphones and loudspeaker, VoLTE calls, SMS and mobile data,
the sensors (through the ADSP), both cameras and the flash, Venus video,
the vibrator, charging and suspend. The SoC reaches VDD minimisation when
idle, and the modem sleeps about 98% of the time.

Not done: the wired headset (USB-C analog), USB host power, GPS, FM radio,
camera colour tuning, and WiFi throughput, which is still below stock.

The sections below are the bring-up log in the order it happened. Dated
notes record what was true then; "Update" notes give the later state.

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
| Main display | 480×640 @ 60 Hz, MIPI DSI (`mdss_dsi_ctrl0`). The exact panel is not yet known. DT candidates for a VGA panel: `gc9503v_jutai_vga_video`, `st7701s_vga_video`, `rouxian_st7701s_boe_video`, `jd9161z_boe_ips_video`. | DSI0 | MDSS/DSI are supported. The panel driver must be generated from the downstream DTB (e.g. with linux-mdss-dsi-panel-driver-generator). **Working:** an ST7701S; a panel driver generated from the stock DT (see "Main display") |
| Outer display | 128×128 @ 30 Hz, driven by MDSS SPI (`qcom,mdss_spi`). Its config comes from the stock dtbo overlay (entry 27), node `qcom,mdss_spi_st7789v2_qvga_cmd`. Despite the name, it is 128×128 and ST7735S-class. | `spi@7af6000` (spi6), CS1 | **Working** with `panel-mipi-dbi-spi`; see "Outer display" below. |
| Touch (main display) | Chipsemi CHSC (`chsc_cap_touch`, driver `semi_touch`) | i2c-3 (`i2c@78b7000`) addr **0x2e**. Unpopulated alternatives in DT: goodix@14, tsc@24, focaltech@38. | **Working:** a Chipsemi CHSC driver in our kernel fork |
| Keypad | `gpio-matrix-keypad`, plus `gpio-keys` (vol_up), PM8916 PON (power) and RESIN | GPIO | Mainline. Row/column GPIOs and keymap need the DTB. |
| Hall sensor (lid) | `hall_sensor` GPIO input | GPIO | Use `gpio-keys` with `SW_LID`. |
| Keypad backlight | `qcom,leds-gpio-keyboard_light` | GPIO | Use `gpio-leds`. |
| Camera flash | `qcom,leds-gpio-flash` + `qcom,camera-gpio-flash` | GPIO | **Working:** `sgm3140`, as on the Nokia 2780 (flash and torch) |
| Vibrator | PM8916 vibrator @ c000 | SPMI | **Working:** mainline `pm8xxx-vibrator` (force feedback) |
| Audio codec | PM8916 analog codec (`analog-codec@f000`) + MSM digital codec, `msm8952-asoc-wcd` card "msm8952-snd-card-mtp", ADSP (Q6/APR) | SPMI + LPASS | Mainline (`msm8916-wcd-analog/digital`, qdsp6). |
| Speaker amplifier | **Awinic AW881xx smart PA** (driver bound) | i2c-5 (`i2c@7af5000`) addr **0x34** | **Working:** an AW88194A, with our own driver (`aw88194.c`; see "Loudspeaker") |
| Speaker amplifier (alt) | Awinic AW8155 GPIO class-D amp (`soc:aw8155`) | GPIO | `simple-audio-amplifier` / `aw8738`-style pulse mode. |
| Headset | 3.5 mm jack with MBHC (Headset Jack + Button Jack inputs) | PM8916 codec | Mainline. |
| USB-C | **WillSemi WUSB3801** Type-C controller | i2c-5 addr **0x60** | Mainline (`wusb3801`). |
| USB-C audio switch | FSA4480 in DT, **not bound** (probably not populated) | i2c-5 addr 0x42 | Mainline (`fsa4480`). |
| USB | ChipIdea HS OTG (`78db000.usb`, `msm_otg`) | | Mainline (`ci_hdrc_msm`). |
| WiFi/BT/FM | WCNSS: Pronto core (`a21b000`) + iris RF chip (probably **WCN3615/3620**, as on other QM215 boards). BT over SMD, FM via `iris-fm`. | | **WiFi and Bluetooth working:** a **WCN3610** (not 3615/3620), on `wcn36xx` with the pending WCN3610 series, `btqcomsmd`; the device's own `wcnss.mdt` and NV. FM: not started |
| Modem | Q6v5 MSS (`4080000.qcom,mss`), BAM-DMUX data, MPSS `SDM439_GENNS_PACK` 3-00078. LTE/GSM/CDMA/IMS features. | | Mainline (`qcom_q6v5_mss`, `bam-dmux`, qrtr). ModemManager works on msm8916-family boards, but not on this one (no network port, see "Modem with a SIM"). SMS works over QMI WMS. **Update 2026-10-08:** VoLTE calls, SMS and mobile data all work (see "Modem with a SIM") |
| GNSS | Via modem (`android.hardware.location.gps`) | | Feasible through the modem's QMI loc service; not started |
| NFC | `nqx` HAL configured and a `pinctrl/nfc` node exists, but **no NFC feature is reported and no NFC i2c device is bound**, so it is likely absent. | | n/a |
| Rear camera | **GalaxyCore GC5035** 5 MP + **DW9714** VCM + `gc5035_otp` EEPROM | CCI `camera@0`, `actuator@0`, CSIPHY | `gc5035` is in the msm89x7 tree (used by Nokia 2780). `dw9714` is mainline. CAMSS 8x17 is enabled in that tree. **Working:** raw frames through CAMSS's RDI path, autofocus with the DW9714; colour balance still needs tuning |
| Front camera | **GalaxyCore GC02M2** 2 MP | CCI `camera@1` | **Working:** a `gc02m2` driver in our kernel fork (fixed focus) |
| Accelerometer | Sensortek **STK8BA53** @ 0x18 | **BLSP i2c bus 4, owned by the ADSP sensor core (SSC)** | `stk8ba50` IIO driver (check compatibility). **Working through the ADSP** (`s22-sensord`, see "Sensors through the ADSP"); the bus stays the ADSP's, since touching it from Linux resets the phone |
| Light/proximity | Sensortek **STK3x1x** @ 0x48 (alternate LTR55x @ 0x23) | same, ADSP | **Working through the ADSP** (`s22-sensord`) |
| Pressure | InvenSense **ICP-10100** @ 0x63 (alternate SPL06 @ 0x77) | same, ADSP | **Working through the ADSP** (`s22-sensord`) |
| Video codec | Venus (`1d00000.qcom,vidc`) | | **Working:** mainline venus with the phone's own `venus.mbn` (H.264, HEVC, VP8 decoding; H.264 encoding) |
| Thermal | tsens @ 4a8000 | | Mainline |

## Linux feasibility: positive

- **QM215 + PM8916 is already a known mainline target.** The [msm89x7-mainline/linux](https://github.com/msm89x7-mainline/linux) tree (branch `msm89x7/7.1.3`) has `qm215.dtsi` and `qm215-pm8916.dtsi`. The closest relative is **`qm215-nokia-weeknd.dts` (Nokia 2780 Flip)**: another QM215 flip phone with the same GC5035 camera, gpio-matrix-keypad, PM8916 charger/BMS, WCN3620, modem, bam-dmux and CAMSS. Copies of those DTS files are in `reference/`.
- The bootloader is unlockable, and people on XDA have already rooted this device with a Magisk-patched V30 boot.img.
- The normal boot route is **lk2nd** (flashed to `boot`) chainloading a mainline arm64 kernel. Although stock is 32-bit, the A53 cores and the msm8916/8917 LK are generally able to boot aarch64.

### Work needed for this device

1. **Main DSI panel driver.** Generate it from the downstream DT panel node (init commands and timings). *Done.*
2. **Outer 128×128 SPI display.** Configure `panel-mipi-dbi-spi` with the init sequence from DT/dtbo, and work out the CS/DC/reset GPIOs. *Done.*
3. **CHSC touch driver.** No mainline driver exists. The touchscreen is a secondary input on a keypad phone, so this is lower priority. *Done: a driver in our kernel fork.*
4. **GC02M2 front camera driver.** Lower priority. *Done: a driver in our kernel fork.*
5. **AW881xx speaker amp.** Identify the exact part and check it against the mainline Awinic drivers. *Done: an AW88194A, with our own driver.*
6. **ADSP-owned sensors.** Either leave them on the ADSP (needs SSC/QMI userspace) or try driving BLSP1 i2c-4 from Linux. *Done: left on the ADSP, with our own daemon (`s22-sensord`).*

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

- **Reaching lk2nd's fastboot from Linux (2026-10-05):** `reboot bootloader` can't work, because the stock bootloader runs first, reads the reboot reason (PON `SOFT_RB_SPARE` bits 7:2), scrubs it and enters its own fastboot. IMEM's restart-reason word doesn't survive the reset (PSCI SYSTEM_RESET power-cycles the SoC). What works: a magic byte (`0x6c`) in PON `DVDD_RB_SPARE` (PMIC 0x88D), which survives the reset and which the stock bootloader neither reads nor clears. Our lk2nd checks it, clears it and stays in fastboot; the kernel's `qcom-pon` exposes it as a one-shot `lk2nd_fastboot` sysfs switch (and clears a leftover at boot). Set it, reboot normally, and the phone lands in lk2nd's fastboot without a key press.
- Stock `fastboot boot` never works for lk2nd here. With the DTB fixes it gets past the DTB check, then `cmd_boot` fails silently ("unknown reason"). Padding the image to the AVB `boot` size (32 MiB) didn't help.
- To go back to Android, flash the stock `dtbo` and `boot` images.

## First mainline boot (2026-10-03)

Booted with `fastboot boot` through lk2nd: kernel `msm89x7/7.1.3` + WCN3610 v3 patches + `qm215-cat-s22flip.dts`, and a busybox initramfs that runs only in RAM.

| Area | Result |
|---|---|
| 4× A53, RAM | Works. **CPU capped at stock's 1209.6 MHz** (960 / 1094.4 / 1209.6): this chip is fused as speed bin 2, while mainline's msm8917 table goes to 1401.6 MHz with no voltages and no CPU supply control. Our DTS overrides `cpu_opp_table` and `pll_opp_table`; the A53 PLL measures 1209600000 Hz under full load |
| Main panel (ST7701S, DSI) | **Works with a real driver:** MSM DRM + DSI + generated `panel-cat-s22flip-st7701s`, plus Adreno 308 probing (see "Main display" below). Early boot uses lk2nd's framebuffer |
| USB gadget (NCM + ACM), telnet | Works once the PM8916 charger module provides extcon |
| Keypad, d-pad, soft/function keys, volume up/down, power, lid | All work. Programmable key = `KEY_NUMERIC_B`; the speaker key (r2c0) sends `KEY_MICMUTE`; `KEY_NUMERIC_A` (r4c4) is unidentified |
| Keypad debounce | Stock 3 ms gives double presses (domes chatter up to ~35 ms); 30 ms in our DTS |
| Suspend (s2idle) | **Works** (4 of 4). Wakes on RTC alarm, power key, lid and keypad (volume up is also a wake source). All four cores and the CPU cluster power down while asleep. Both displays blank and come back (console on the main panel, splash on the outer one), and the USB network link reconnects by itself. Not yet checked: WiFi/modem across suspend, battery drain while asleep (needs the phone unplugged). `mem` is the same s2idle; there's no deeper state |
| Charger (LBC) + BMS | Charging, capacity reported |
| WCNSS + WCN3610 | **WiFi works end to end:** scan (2.4 GHz only), WPA2-PSK/CCMP association on 802.11n, DHCP, DNS, NTP and HTTPS. Tested with `wpa_supplicant` from a RAM-only Alpine root. **Bluetooth works:** a Bluetooth LE keyboard pairs and types, and A2DP audio (SBC) plays to a classic speaker through PipeWire (bluez 5.87). A2DP used to stutter while WiFi transferred data: wcn36xx started the WCN3610 in BTC execution mode 2 (PTA only). Qualcomm's prima default, mode 0 (smart), keeps A2DP smooth during a sustained download with the same WiFi throughput when Bluetooth is idle; mode 5 (A2DP-weighted) starves WiFi association. Our kernel now defaults to 0 (`btc_mode` module parameter for testing). Two more settings matter because the radio is shared: **WiFi power save** (with it on, the firmware held packets: ping to the router ~0.8 s) and **BlueZ's background LE scan** for trusted devices (the kernel default listens 30 ms of every 60 ms, so Bluetooth held the radio about half the time even with nothing connected). With power save off and that scan at 11.25 ms per 1.28 s (`ScanIntervalAutoConnect=2048`, `ScanWindowAutoConnect=18` in BlueZ's `main.conf`), ping is 5–12 ms with Bluetooth on and a LAN download went from 0.39 to 1.27 MB/s |
| eMMC, microSD | Detected |
| WUSB3801 Type-C | **Works.** Registers `port0` (sink/device), partner detected, 3.0 A. Root cause of the old first-probe failure: the **first transfer on I²C bus 0 after boot is lost** (NACKed, `-ENXIO`) whatever the address or timing, while SDA/SCL read idle-high before and after; it's a QUP controller first-activation glitch, not the PMIC or the chip. Fix in our fork: buses marked `cat,first-transfer-lost` (only `blsp2_i2c1`) get one dummy 1-byte read to the reserved address 0x7f in `i2c-qup` probe, before any client sees the bus; the WUSB3801 now initializes on its first try (3 of 3 boots, the last without any driver retry). An earlier driver-side retry (5 × 10 ms on `-ENXIO`) was committed and then reverted; it's in the `s22flip` history if the problem shows up elsewhere. Datasheet notes: ENB is active low, so stock's `wusb3801,reset-gpio = 12` (really the outer backlight) is bogus |
| USB host (OTG) | **Probably data-capable, but no VBUS.** Stock sets the controller to OTG (`qcom,hsusb-otg-mode = 3`) and the WUSB3801 to dual-role (`drp-toggle-time`, `host-current`), but the USB node has no VBUS supply and the PM8916 linear charger has no OTG boost; no external 5 V boost regulator exists in the stock DT. Devices (even self-powered hubs) normally wait for host VBUS before connecting, so host mode would need an adapter that injects external 5 V, outside the Type-C spec. Untested. Bluetooth is the simpler route for a keyboard |
| Headset (USB-C analog) | **No 3.5 mm jack: audio over USB-C.** Stock has an **FSA4480** analog switch (`qcom,fsa4480-i2c`, I²C `0x7af5000` @ 0x42, the WUSB3801's bus), `qcom,msm-mbhc-usbc-audio-supported`, and two "USB-C analog enable" GPIOs (`msm_cdc_pinctrl_cdc_usbc_audio_en1`/`en2`). Mainline has `fsa4480` (Type-C mode switch, not enabled yet) and `wusb3801` already reports `TYPEC_ACCESSORY_AUDIO`. Needs: the FSA4480 node linked to the Type-C port, the enable GPIOs, PM8916 MBHC detection and buttons, a mixer path. Only **passive** adapters/earbuds can work (digital USB-C audio needs host VBUS). Untested |
| Outer display | Works: `panel-mipi-dbi` on SPI CS1, built-in driver, boot splash drawn by init (see below) |
| Modem | **Registers on LTE and receives SMS** (see "Modem" and "Modem with a SIM" below): boots, QMI over QRTR, IMEI and firmware revision match stock, picks the carrier's MCFG profile itself. Calls and data not yet; bam-dmux never comes up. **Update 2026-10-08:** calls, texts both ways and mobile data work; bam-dmux comes up once the AP asks for it and opens the port (see "Modem with a SIM") |
| Sensors (ADSP) | **Accelerometer, proximity, light and pressure all work** through the ADSP with our own daemon `s22-sensord` (see "Sensors through the ADSP"). The sensor bus is locked to the ADSP |
| Audio | **Earpiece, both built-in mics and the loudspeaker work** (see "Audio" below): ADSP + QDSP6 sound card `cat-s22flip`, PM8916 codec, AW88194A amp on Quinary MI2S with our own driver, speaker protection DSP running. Headset jack untested |
| Venus | Needs `venus.mbn`, which isn't in the ramdisk. Disabled in our DT until then: a Venus that never probes blocks GCC's sync_state, which kept the display power domain on. **Update 2026-10-07:** enabled with the phone's own `venus.mbn` and the `venus_mem` carve-out; decoding and encoding work |

lk2nd (msm8952) ignores boot header addresses: kernel at `0x80080000`, DTB at `0x83400000`, ramdisk at `0x83600000`. The ramdisk must end below the reserved region at `0x85b00000`.

## Outer display (working, 2026-10-03)

- **Controller:** ST7735S-class, 128×128 RGB565. Stock calls it "st7789v2 qvga", but the init sequence is ST7735S-style (B1–B4, C0–C5, E0/E1, FC).
- **Bus:** BLSP2 QUP2 SPI (`blsp2_spi2`), **chip select 1** (GPIO 22), 50 MHz. Only CS1 is muxed; CS0 (GPIO 47) is left alone. Data pins are GPIO 20/21/23.
- **Control lines:** D/C on GPIO 68, reset on GPIO 125, TE on GPIO 124 (unused).
- **Supplies:** L17 at 2.85 V and L6 at 1.8 V.
- **Backlight:** GPIO 12, on/off only (`gpio-backlight`). Stock's DT says `bl_gpio_pulse`, but the stock kernel's `mdss_spi_panel_bl_ctrl` (disassembled from the V30 boot.img) only drives the GPIO high or low; turning it off can be delayed by an alarm timer. A test with mainline's 32-step `ktd253` pulse driver produced no visible dimming at any level, so there is no dimming to support. Stock's DT also lists GPIO 12 as the WUSB3801 reset, which the datasheet rules out (ENB is active low).
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

## Modem (2026-10-03)

How it was brought up (RAM only; nothing is written to the eMMC):
1. Fetch `mba.mbn` and `modem.mbn` (squashed from the stock `modem` partition) over USB into `/lib/firmware/qcom/qm215/cat/s22flip/`.
2. Read `modemst1`, `modemst2`, `fsc` and `fsg` from the eMMC into RAM files named `modem_fs1`, `modem_fs2`, `modem_fsc` and `modem_fsg` (`tools/phone/efs-to-ram.sh`).
3. Run `rmtfs -o <that dir> -r -v` (Alpine `rmtfs`) in directory mode. `-r` means it never writes storage.
4. `echo start > /sys/class/remoteproc/remoteproc1/state` (`tools/phone/modem-up.sh`).

Results:
- **Boot:** `MBA booted without debug policy, loading mpss` → `remote processor 4080000.remoteproc is now up`. The modem reads its EFS through rmtfs. The rmtfs shared memory at `0x92100000` (mainline dtsi) works; stock allocated the same 1.5 MB dynamically.
- **QMI:** QRTR node 0 publishes the full set (DMS, NAS, WDS, UIM, Voice, WMS, ...). `qmicli -d qrtr://0` works. No `pd-mapper` or `tqftpserv` was needed.
- **Identity:** IMEI present and identical to fastboot's. Revision `MPSS.JO.3.3-00078-SDM439_GENNS_PACK-1.419755.4.425674.1`, the same as stock.
- **Operating mode:** reports `shutting-down` after boot. `--dms-set-operating-mode=online` brings it online.
- **Without a SIM:** UIM reports `no-atr-received`, and network scan is refused (`InvalidOperation`). About 20 s after going online it selects LTE and reports serving and neighbour cell measurements (RSRP around −108 to −113 dBm), so the RF and calibration work.

Notes:
- Android's rmtfs kept syncing `modemst1` after the backup was taken. That copy differs, and the newer one is saved as `backup/partitions/modemst1.after-android-2026-10-03.img`.
- The host's ModemManager probes the gadget's ACM serial port and garbles that shell. Use telnet (`tools/s22sh` now does).

## Modem with a SIM (2026-10-05)

A prepaid SIM from a T-Mobile MVNO, in the installed system (rmtfs serving the
EFS from files, tqftpserv for the MCFG indexes):
- **Registration:** LTE, CS and PS attached, not roaming, about 20 s after
  boot. The modem selected the `Commercial-TMO` MCFG software profile by
  itself (`--pdc-list-configs=software`), with the carrier's APNs as profiles
  1-3 (internet, `ims`, `sos`). Voice domain preference is `ps-preferred`.
- **Operating mode:** still reports `shutting-down` after boot, so something
  has to set it online.
- **SMS works, through QMI WMS:** routes for every message class set to NV
  storage with store-and-notify (the modem acks to the network), new-message
  indications on, then raw read, decode the 3GPP PDU (SMSC address first),
  and delete. The carrier's multi-part welcome text and a text from another
  phone both arrived. qmicli's WMS support stops at routes, so this needs a
  libqmi client of its own (`tools/s22-smsd`, Python through GObject
  introspection).
- **ModemManager does not work here:** it refuses the modem with "Failed to
  find a net port in the QMI modem". Its QMI modems need a network port, and
  the `bam-dmux` netdevs only appear once the modem raises its SMSM
  power-collapse bit, which this firmware never does, online or not, with or
  without a SIM. (Update: it raises the bit only when the AP asks first; see
  "Bringing up data on bam-dmux" below. ModemManager was not tried again.) The AT port (`wwan0at0`) answers `AT+CPIN?`/`AT+CFUN?` but
  has no SMS commands.
- **Data:** WDS reports `disconnected` (nothing on the Linux side can use the
  LTE default bearer without bam-dmux), and the firmware refuses the
  autoconnect query (`InvalidOperation`). (Update 2026-10-08: mobile data
  works; see the last item.)
- **Phone number:** `qmicli --dms-get-msisdn` reads the number the network
  wrote to the SIM.
- **Why the modem never slept (2026-10-06):** its own IMS stack asks the AP
  to bring up the IMS PDN, over QMI service 770 ("IMS data service", hosted
  by Android's imsdatadaemon), and retried forever when nobody answered: the
  modem stayed RRC connected and transmitting, 0 power collapses ever. Its
  debug messages (read over DIAG from Linux: SMD channels DIAG/DIAG_CNTL, a
  minimal feature mask, raw commands, HDLC-framed replies, F3 masks set over
  the control channel) said so: "QMI send failed !! IMS PDN cannot be brought
  up on AP". `tools/s22-imsd` hosts service 770 on QRTR: the modem sends
  three setup requests and a PDP Activate request (APN `ims`, IPv6, WDS
  profile 2 here); s22-imsd opens the DPM port, sets the raw-IP data format
  on bam-dmux, starts that profile with WDS and returns the address in the
  PDP Activate indication (layouts as in libqmi's IMSDCM definitions). The
  modem then power-collapses about every 0.6 s, asleep ~98% of the time.
  IMS registration needs the operation result (field 0x02) in that
  indication too (documented by 81voltd): without it the modem never tries
  to register and releases the PDN after ~15 minutes, in a 20-minute cycle.
  With it IMS registers within seconds (voice, SMS and UT over LTE) and the
  modem is asleep ~98% of the time. The network offers voice only over IMS.
- **VoLTE calls work (2026-10-06):** QMI Voice for call control; the audio
  is the modem's voice session on the ADSP, started by holding the hostless
  q6voice VoiceMMode1 PCM open in both directions (playback with the stop
  threshold at the boundary, or it stops on an underrun at once) and routed
  with the q6routing voice mixers to Primary MI2S (earpiece) and from
  Tertiary MI2S (keypad microphone). `tools/s22-call`: ring, answer by
  opening the flip or the Call key, hang up by closing it or End.
- **Bringing up data on bam-dmux:** the modem raises its bam-dmux power bit
  only when the AP asks first (bam-dmux runtime resume), and opens channels
  only after a DPM open port (control `DATA5_CNTL`, hardware data port
  bam-dmux endpoint 0). Then: WDA raw-IP with that endpoint, WDS bind mux
  data port, start network.
- **Mobile data (2026-10-08):** the internet PDN (WDS profile 1) runs on
  bam-dmux channel 1, beside the IMS PDN on channel 0. Each channel opens,
  and its `wwan` interface appears, only once its own DPM port is open
  (`DATA5_CNTL` is channel 0, `DATA6_CNTL` channel 1). The network is
  IPv6-only: an IPv4 call gets a 464XLAT placeholder address (192.0.0.2),
  and IPv4-only names arrive as DNS64 addresses.

## Main display (working, 2026-10-03)

- **Driver:** `panel-cat-s22flip-st7701s`, generated with linux-mdss-dsi-panel-driver-generator from the stock node `qcom,mdss_dsi_st7701s_vga_video` (`-r vdd -r vddio`). It lives in `drivers/gpu/drm/panel/msm89x7-generated/`. 480×640 @ 60 Hz, 2 lanes, RGB888, burst video mode; reset on GPIO 60, active low.
- **lk2nd handoff:** the DTS panel node uses `compatible = "cat,s22flip-panel"`. lk2nd replaces it with the detected panel (`cat,s22flip-st7701s`), which the driver matches.
- **Supplies (per stock):**
  - DSI `vdda` and `vddio` on L6, 1.8 V. That differs from msm8916 boards, which use L2 at 1.2 V for `vdda`.
  - Panel `vdd` on L17 (2.85 V) and `vddio` on L6.
  - DSI PHY `vddio` on L6, in LDO mode.
- **Backlight:** `pwm-backlight` on the PM8916 LPG, routed to MPP4. **The LPG reaches MPP4 over DTEST2, not DTEST1** as on the msm8916 reference designs: the bootloader leaves MPP4 MODE_CTL at `0x1a` (digital output, source 5 = DTEST2). With DTEST1 the panel worked but stayed dark. Confirmed by booting with the backlight path untouched and reading the registers through regmap debugfs. Brightness control works (100 µs period, as stock).
- **GPU:** MSM DRM also brings up the Adreno 308 (`a300_pm4.fw`/`a300_pfp.fw`).
- **Console:** the boot logos (Tux) disappear when MSM DRM replaces lk2nd's framebuffer.

## Audio (2026-10-03)

The ADSP boots `adsp.mbn` (shipped in the bring-up ramdisk) and the mainline QDSP6 sound card (`qcom,msm8916-qdsp6-sndcard`) comes up as `cat-s22flip`. The PM8916 analog codec handles the earpiece, headset and mics, behind the LPASS digital codec. Playback uses Primary MI2S (`hw:0,0`, MultiMedia1) and capture uses Tertiary MI2S (`hw:0,1`, MultiMedia2).

| Path | Mixer settings (`amixer -c0 cset name=...`) |
|---|---|
| Earpiece | `PRI_MI2S_RX Audio Mixer MultiMedia1` = 1, `RX1 MIX1 INP1` = RX1, `EAR_S` = Switch, `RX1 Digital Volume` = 84 (0 dB) |
| Capture (both) | `MultiMedia2 Mixer TERT_MI2S_TX` = 1, `CIC1 MUX`/`CIC2 MUX` = AMIC. DEC1 feeds the left channel and DEC2 the right |
| Mic 1: under the keypad, near **#** (AMIC1) | `DEC1 MUX` = ADC1, gain `ADC1 Volume` |
| Mic 2: the hole above the outer display (AMIC3) | `ADC2 MUX` = INP3, `DEC1/2 MUX` = ADC2 or ADC3 (identical data), gain **`ADC3 Volume`**. AMIC3 is amplified by the TX3 stage, not TX2 |

The mic locations were found with a tap test, recording both mics in stereo. At `ADC1/3 Volume` = 4, speech held as for a call measured about −44 dBFS on mic 1. Final gains belong in a UCM profile.

### Loudspeaker (AW88194)

The amp is an Awinic AW88194 (chip ID 0x1806, product ID 1, DSP product ID 0x0000) at 0x34 on `blsp2_i2c1`. Reset (active low) is GPIO 66, and the interrupt (unused) is GPIO 59. It is fed by **Quinary MI2S**, which goes out on the `pri_mi2s` pins: GPIO 85/88 BCK and data, GPIO 87 WS, GPIO 86 the amp's feedback line. Mainline has no driver for it, so our branch adds `sound/soc/codecs/aw88194.c` and `awinic,aw88194.yaml`:

- **Profile:** it loads stock's `aw881xx_acf.bin` (from `/vendor/firmware`, not redistributed) and applies the `aw88194`/"Music" register profile.
- **Power-up:** it powers up on unmute with the datasheet sequence: `PWDN=0`, wait for PLL lock, `AMPPD=0`, wait for `SWS`, then `HMUTE=0`.
- **Controls:** the I2S format follows `hw_params`. "Speaker Volume" works in 0.5 dB steps (0 to −96 dB) and starts at −12 dB while the DSP is bypassed.
- **Playback:** `QUIN_MI2S_RX Audio Mixer MultiMedia1` = 1, then play on `hw:0,0`.

**Speaker protection DSP: works** (on by default; `dsp=0` bypasses it).

- **The trap:** the amp's register product ID (0x79) reads 1, which suggests an AW88194. But **the DSP is the AW88194A revision**: its own product ID at DSP address 0x1f80 reads 0x6e90.
- **What happened with the AW88194 profile:** the DSP firmware loads and starts executing, but never comes up. The watchdog register 0x42 stays 0 and the speaker model is empty.
- **What stock does:** it loads the AW88194 profile, reads the DSP product ID, then reloads the `aw88194A` profile.
- **What our driver does:** it parses both profiles at probe. Once the PLL locks on the first stream, it reads the DSP ID and switches. 0x1f80 only reads back with the PLL running, which is why an early read returned 0x0000. It then loads the DSP firmware to 0x8c00, the config to 0x8600 and `VCALB` (0x37f9) to 0x866d, and checks that `WDT` is non-zero after enabling the DSP.
- **Result during playback:** the state matches stock register for register: `SYSCTRL` 0x6440, `SYSST` 0x1311/0x3311, `WDT` counting, and the AGC registers 0x09/0x0a/0x0b = 0x5e69/0x0f06/0x0f06.
- **Volume:** the default is stock's −4 dB, and the volume control writes the amp's `VOL` register. A UCM profile should use "Speaker Volume" as the hardware playback volume.

**How the reference was obtained: stock kernel, no Android** (`tools/mkstockref.sh`, `initramfs/stockref-init`).

- **Boot image:** the stock v30 32-bit kernel with stock's SoC DTB (entry 12) plus the board overlay (`dtbo` entry 27) merged by `fdtoverlay`. The DTB is appended to the zImage, because lk2nd rejects a header-v2 DTB with "dtb not found". The root is an Alpine armv7 RAM-only root.
- **Command line:** `panic=3 msm_poweroff.download_mode=0` makes a crash reboot back to lk2nd instead of entering Qualcomm crash-dump mode (05c6:900e).
- **Init:** sets all subsystems to `restart_level=related`. USB networking has to be **RNDIS**, because the stock kernel crashes about 25 s after binding an NCM gadget. Telnet needs `/dev/ptmx` → `pts/ptmx`, and there's a raw `nc` shell on port 2323 (`S22_PORT=2323 tools/s22sh`).
- **Audio stack:** Qualcomm's audio is vendor modules (`audio_apr`, `audio_adsp_loader`, `audio_q6`, `audio_platform`, codecs, `audio_machine_sdm450`). They are extracted from the `super` backup with `lpunpack` and `debugfs`, then loaded in `modules.load` order. Then `/sys/kernel/boot_adsp/boot`, `/dev/snd` nodes created by hand, and `QUIN_MI2S_RX Audio Mixer MultiMedia1` = 1.
- **Reading the amp:** the stock driver's `reg` and `dsp_rw` sysfs files give its registers and DSP memory. Avoid reading its `dsp` file mid-stream: it dumps the whole firmware over I²C and stalls the audio.
- **The eMMC** is never mounted or written.

**USB drop fixed along the way:** `qm215-pm8916.dtsi` gave the USB PHY `v1p8`/`v3p3` supplies, but the 28nm femtophy driver reads `vdda1p8`/`vdda3p3`. With the wrong names the PHY never held L7/L13 on, and switching the codec's mic bias (also on L13) off cut USB. Both are fixed in our kernel branch.

## Sensors through the ADSP (2026-10-04)

The accelerometer, light/proximity and pressure sensors sit on BLSP1 I²C-4 (0x78b8000, GPIO 14/15), which the **secure firmware locks to the ADSP**. Enabling `blsp1_i2c4` in Linux resets the phone about 12 s into boot, even without `oops=panic`. So the sensors are only reachable through the ADSP's sensor manager (SMGR), as on stock. Everything below is clean-room, worked out from the phone's own binaries and live traffic. Leaked Qualcomm sources exist on GitHub, but none were used.

- **Topology:** QRTR node 5 is the ADSP and node 7 is WCNSS. WCNSS advertises a stub "Sensor Manager" (256 v0) that just echoes requests; ignore it.
- **What the ADSP needs:** a **sensor registry service** on the phone side: QMI service 0x10f (271), version 2, instance 0. It also needs the **time service 0x118 (280) v2, instance 50** to exist; it is never actually called. Stock's `sensors.qti` provides both.
  - The ADSP reads about 60 registry groups with request **0x04** (TLV 0x01 = u16 group ID).
  - The answer is TLV 0x02 = result (**u16 only, length 2**), TLV 0x03 = group ID, and TLV 0x04 = u16 count plus data.
  - It also sends one request **0x01** (version), answered with TLV 0x03 = u32 46 and TLV 0x04 = u16 6.
- **Registry data:** `/persist/sensors/sns.reg` is a flat image. The group table (u16 size, offset, ID triples) lives in `sensors.qti`. `tools/sns-reg-groups.py` extracts it from the phone's own binary; all 59 group answers stock gave in a trace match byte for byte.
- **Result:** with `tools/sns-reg-serve` running, the ADSP publishes the real **SMGR, service 256 v1, instance 50 on node 5**, plus about 20 more sensor services. "All sensor info" (request 0x05) lists **ACCEL (ID 0), PROX_LIGHT (ID 40), PRESSURE (ID 30)**.
- **Streaming:** SMGR request **0x02** (periodic report) takes:
  - TLV 1 = report ID (u8), TLV 2 = action (1 = add), TLV 3 = rate in Hz (u16), TLV 4 = buffer factor (u8)
  - TLV 5 = count (u8) plus 19-byte items: sensor ID, data type, sensitivity, decimation (u8 each), rate (u16), five u8 options, two u32 thresholds
  - Indication **0x03** carries TLV 4 items: sensor ID, data type, then x/y/z as s32 **Q16 m/s²**, then a u32 timestamp in 32768 Hz ticks, then flags, quality and sensitivity (u8 each).
- **Accelerometer axes**, measured by tilting: the sensor reports the direction of gravity (flat face up: z = −9.7). In Android/Linux convention, **X = sensor Y, Y = sensor X, Z = −sensor Z**.
- **The proximity sensor** sits near the earpiece; it's for turning the screen off at the ear.
  - **PROX_LIGHT data type 0** = proximity: x = near (non-zero) or far, y = raw count (about 240 open, about 2,000–4,000 covered).
  - **Data type 1** = light: x = lux in Q16, from 0 covered to about 15 indoors, saturating at 32,767 under a flashlight.
  - **PRESSURE data type 0** = hPa in Q16 (about 927). It has no data type 1.
- **`tools/s22-sensord`** does all of this in one static daemon:
  - provides the registry and time services
  - finds the ADSP's SMGR by QRTR lookup, re-requests streams if the ADSP restarts, and ignores the WCNSS stub
  - streams accel/proximity/light/pressure (default 10/5/2/1 Hz; `-a -p -l -b`, 0 disables a sensor)
  - outputs a uinput **"S22 Flip accelerometer"** (`INPUT_PROP_ACCELEROMETER`, milli-g, resolution 1000, Linux axes, for iio-sensor-proxy), a uinput **"S22 Flip proximity"** switch (`SW_FRONT_PROXIMITY`), and files in `/run/s22-sensors/` (`accel`, `proximity`, `light`, `pressure`)
  - is tested from a cold boot with nothing else running
- **Tools:**
  - `tools/qmisend.c`: send a raw QMI request or listen as a service, over QRTR
  - `tools/sns-reg-serve.c`: the standalone registry server
- `tools/s22-sensord.c`: the sensor daemon
  - `tools/sns-reg-groups.py`: extracts the group table

  `sns.reg` and the group table come from the phone and are not redistributed.

## Next steps

- **WiFi throughput (parked 2026-10-05):** mainline wcn36xx settles at MCS 2 in both directions
  under load (LAN about 1.4-1.5 MB/s down, 0.8 MB/s up at -46 dBm). The
  router starts higher (MCS 5-7 right after association) and falls back once
  traffic flows. Stock prima on the stock kernel (same router and NV file,
  Bluetooth off, -60 dBm) moves 1.8-3.0 MB/s down and about 1.2-1.5 MB/s up,
  so there is a real gap of roughly 1.5-2x, mostly on upload. Prima's
  reported 72.2 Mbit/s (MCS 7, short GI) is not a measurement: its default
  `gReportMaxLinkSpeed` reports the maximum for the signal level. Ruled out on
  mainline: Bluetooth (same with BT powered off), the advertised HT
  capabilities (stock's 0x012c makes no difference), block-ack sessions (up
  in both directions, window 64 as on prima), the receive path (CPU mostly
  idle, no drops), the radio's supply rails (same as stock), the XO mode
  (19.2 MHz, as stock), and prima's BSS/station parameters (basic rate set,
  protection mode, MIMO power save, retry limits). One real bug was found
  and fixed: with a router that offers no 802.11b rates, wcn36xx told the
  firmware the router had no legacy rates at all. EDCA (which wcn36xx never
  configured) made no difference either, nor did reporting the battery level
  to the firmware as the downstream driver does. Against a different access
  point (a phone hotspot at -33 dBm) the link also settles at MCS 2 or below
  under load, so the cause is on the phone's side, not the router's. Also
  matching stock with no effect: the per-frame TX descriptor, and pull-up on
  the WCN 5-wire pins (GPIO 76-80 run pull-down, unlike stock's active state).
  Prima's complete start configuration (177 settings, captured from its own
  trace output) and its 23 dBm power cap made no difference either.
  Deep sleep (2026-10-05): the RPM's own counters (XO shutdown "vlow", VDD
  minimization "vmin", per-master sleep stats in RPM message RAM) showed
  none ever entered. Two causes fixed in our kernel: msm8917 lacked the CPU
  cluster idle state that notifies the RPM (downstream "l2-pc", PSCI
  0x41000053), and the msm8916 digital codec held its master clock (an ADSP
  clock) from probe, which kept the ADSP awake; it now follows the bias
  level. CPUs, ADSP and WCNSS now sleep; the modem never does (no SIM, also
  in low-power and offline mode), so vlow/vmin stay at 0. Suspend, lid
  closed, USB data connected: 141 -> 122 mA; awake idle lid closed ~165 ->
  ~145 mA. The modem also needs a TFTP server (stock: tftp_server) for its
  MCFG indexes (modem_pr/mcfg/...); tqftpserv serves them.
  The RPM-notifying cluster state needs the MPM wake controller: in it the
  CPUs wake only for MPM pins and the MPM's timer. Without the MPM described,
  kernel timers stopped firing once the modem was stopped ("sleep 1" never
  returned, and reboots hung, since shutdown stops the modem). Now described
  in our kernel: the vMPM in RPM message RAM (0x601d0, 64 pins, woken by GIC
  SPI 171, told through APCS IPC bit 1), GIC pins from the downstream
  msm8937 table (tsens 2, USB 49/58, PMIC arbiter 62; pin 53 is shared with
  GPIO 62 and left to the GPIO), the GPIO wake map (47 GPIOs, including the
  lid, volume up and keypad rows), and the CPU cluster power domain as its
  child. Mainline's MPM driver never wrote the next wakeup time into the
  vMPM timer words, which downstream does before every sleep; ours does.
  With the modem stopped, timers keep time and reboots take about 55 s.
  Once the modem slept, the RPM did reach VDD minimization in that state, but
  stopped XO under active peripherals (the outer display's SPI transfers
  timed out, the display controller wedged): GCC here is fed by a fixed
  board XO and never votes XO in the RPM's sleep set, which downstream's
  peripheral clocks do. The RPM-notifying state is out again until GCC
  holds that vote.
  Update 2026-10-06: fixed. GCC and the other real XO users now take XO
  from the RPM's XO clock, which votes in both sets while enabled, and
  idle hardware lets go of it: the digital codec's AHB stand-in is back
  on the board clock, and the USB glue runtime-suspends without a cable.
  With the lid closed and the cable out, the RPM reached VDD minimisation
  8137 times in 10 minutes, with the cluster state kept. Lazy RCU, PSI off
  and sensors on demand cut the remaining idle wakeups.
  Also: cpuidle-psci creates its device once, and gave up when the CPU power
  domains (now waiting for the MPM) were not there yet; ours retries.
  Display wedge on lid open (2026-10-05): "hw recovery is not complete for
  ctl:1", then a blue screen. GCC's sync_state never ran because Venus (no
  firmware) never probed, so the display power domain left on by the
  bootloader's splash never switched off; DPU registers kept stale flush and
  reset state across display off/on. With Venus disabled until it is set up,
  the display domain powers off with the lid closed and 60 off/on cycles ran
  clean; the DPU also no longer waits for a stale flush on a disabled
  encoder.
  WiFi power, measured with a USB inline meter (2026-10-05, screen on, battery
  full, linear charger so input current = system current): idle with power
  save off ~275 mA total, power save on ~258 mA (-17 mA), downloading ~298 mA
  (+23 mA), interface down ~241 mA (-34 mA).
  What the hardware is certified for (public FCC filing ZL5S22F, WiFi
  report SZ21010168W07): 802.11b/g/n on channels 1-11, HT20 MCS 0-7 (up to
  65 Mbit/s, 72.2 with short GI) and HT40 on channels 3-9, about 17-18 dBm
  average conducted power, PIFA antenna with 0.18 dBi gain.
- **Wired headset over USB-C:** FSA4480 + Type-C audio accessory mode + PM8916
  MBHC (see the table above); needs a passive USB-C to 3.5 mm adapter.
- **FM radio:** the WCNSS iris receiver; no mainline driver. The antenna is
  probably the USB-C headset cable.
- **GPS:** through the modem's QMI location service; not started.
- **Cameras:** both work (rear GC5035 with DW9714 autofocus, front GC02M2,
  flash as a torch); automatic white balance leaves a red tint and needs
  tuning.
- **Smaller items:** a UCM profile for the built-in audio. (Done: the
  touchscreen, Venus, the keypad backlight as GPIO LED
  `white:kbd_backlight`, lit by s22-t9d on key presses.)
- **Modem with a SIM:** done. Registration, SMS both ways, VoLTE calls,
  mobile data and the modem's sleep all work (see "Modem with a SIM").
  Not done: MMS and GPS.

## Contents of `dump/`

- `props/`: getprop, feature list, soc0 info
- `proc/`: cpuinfo, meminfo, interrupts, devices, etc. (some files contain "Permission denied")
- `sys/`: DT node tree (`devicetree_paths.txt`), every bus device with its bound driver (`bus_devices_uevents.txt`), drivers, input, regulators, thermal, power supply, cpufreq
- `dumpsys/`: display, input, sensors, camera, audio, telephony, wifi, lshal, getevent
- `logs/`: logcat (all buffers)
- `fs/vendor`: the readable 65 MB of /vendor (mixer paths, audio platform info, camera config and chromatix, sensor registry, WCNSS_qcom_cfg.ini, init rc, HALs, audio and WLAN kernel modules)
- `fs/system`, `fs/product`: keylayouts and etc
