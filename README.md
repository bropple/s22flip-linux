# Mainline Linux on the Cat S22 Flip

Notes, tools and bring-up scripts for running a mainline Linux kernel on the
**Cat S22 Flip** (Qualcomm QM215 + PM8916, 2 GB RAM, 480×640 ST7701S DSI panel,
128×128 SPI outer display, WCN3610 WiFi/BT).

It boots: four cores, the main ST7701S panel on a real DSI driver (with
backlight control and the Adreno 308 GPU), the 128×128 outer display,
USB networking with a shell, the full keypad, lid switch, charging, and WiFi
(scan, WPA2, DHCP and HTTPS from a RAM-only Alpine userspace), and the modem
(boots, QMI, goes online and measures LTE cells; not tested with a SIM).
See [HARDWARE.md](HARDWARE.md) for the hardware survey, everything learned
about the bootloader, and the current status.

## Related branches

| What | Where |
|---|---|
| Kernel: `msm89x7/7.1.3` + WCN3610 v3 patches + config fragment + `qm215-cat-s22flip.dts` | [bropple/linux `s22flip`](https://github.com/bropple/linux/tree/s22flip) |
| lk2nd: QM215 entry that boots on this phone (SoC-only DTB, `__symbols__`, ST7701S panel) | [bropple/lk2nd `s22flip`](https://github.com/bropple/lk2nd/tree/s22flip) |
| Minimal DTBO the stock bootloader needs before it will start lk2nd | [bropple/dtbo-lk2nd `s22flip`](https://github.com/bropple/dtbo-lk2nd/tree/s22flip) |

## Booting

> [!WARNING]
> Unlocking wipes the phone. Back up your own `persist`, `modemst1`, `modemst2`,
> `fsg` and `fsc` partitions first (`tools/backup_partitions.sh` on a rooted phone).
> They hold your IMEI and radio calibration, and nobody else's copy will work.
> Never flash `aboot`, `sbl1`, `tz` or `rpm`.

1. Enable OEM unlocking, then `fastboot flashing unlock`.
2. From stock fastboot (Volume Down + Power from off), flash the minimal DTBO and lk2nd:
   ```
   fastboot flash dtbo dtbo-cat-s22flip.img
   fastboot flash boot lk2nd.img
   ```
3. Generate the outer display's init sequence from your own phone's stock
   `dtbo.img` (from the OTA or the `dtbo` partition). Entry 27 holds the panel node:
   ```
   mkdtboimg dump dtbo.img -b entry
   tools/mkmipidbi.py entry.27 /fragment@31/__overlay__/qcom,mdss_spi_st7789v2_qvga_cmd \
       firmware/lib/firmware/cat,s22flip-ext-panel.bin
   ```
4. Build a kernel from the `s22flip` branch with the postmarketOS `msm89x7` config
   plus `arch/arm64/configs/s22flip.config`, then build the bring-up image with
   `initramfs/build.sh` and boot it from lk2nd's fastboot:
   ```
   fastboot boot out/s22flip-bringup.img
   ```
   The phone appears as a USB network device at `172.16.42.1` (telnet) plus an
   ACM serial shell. `tools/s22sh 'command'` runs a command over telnet.

To go back to Android, flash the stock `dtbo` and `boot` images for your firmware version.

## Tools

| File | Purpose |
|---|---|
| `collect.sh` | Collect hardware info over (unrooted) ADB |
| `tools/imgpatch.py` | Apply Android OTA `IMGDIFF2` patches off-device (rebuilds v30 images from the v29 full OTA + v30 incremental) |
| `tools/backup_partitions.sh` | Stream every partition except `userdata` off a rooted phone, with SHA-1 checks against the device |
| `tools/s22sh` | Run a command on the bring-up initramfs over telnet (the host's ModemManager garbles the ACM serial port) |
| `tools/phone/` | Phone-side scripts: copy EFS into RAM, start `rmtfs -r` and boot the modem |
| `tools/mkrootfs.sh` | Build the Alpine aarch64 toolbox root (WiFi tools etc.) that the phone fetches into RAM over USB |
| `tools/mkmipidbi.py` | Convert a stock `qcom,mdss-spi-on-command` into a `panel-mipi-dbi` init-sequence firmware file |
| `initramfs/` | Busybox bring-up initramfs (RAM only; never mounts the eMMC), the boot image build script, and the outer-display splash (`splash/make-splash.sh`) |

Firmware is not included. The signed modem, WCNSS, ADSP and Venus images must
come from your own phone (the `modem` partition) or its OTA package.
