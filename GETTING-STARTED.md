# Getting started: mainline Linux on your Cat S22 Flip

This walks you from a stock phone to a mainline kernel booting from RAM, and
then to a Linux system installed on the eMMC. Everything here was done on a
real S22 Flip (firmware V30). Read [HARDWARE.md](HARDWARE.md) for the details
behind each step.

> [!CAUTION]
> This is for people comfortable with a shell, fastboot and the risk of a
> dead phone. Two rules keep that risk small:
> - **Never flash `sbl1`, `tz`, `rpm`, `aboot`** (or `devcfg`, `cmnlib*`,
>   `keymaster`, `dsp`, `modem`...). The phone's chain of trust is enforced in
>   hardware; a broken bootloader partition has no known recovery.
> - **Back up your own EFS and `persist` before anything else.** They hold
>   your IMEI and radio calibration. Nobody else's copy will work for you.

## What you need

- A Cat S22 Flip and a USB-C cable.
- A Linux PC with `fastboot`/`adb` (android-tools), `git`, `make`, `dtc`.
- Cross compilers: `aarch64-linux-gnu-gcc` (kernel, tools) and
  `arm-none-eabi-gcc` (lk2nd).
- `mkdtboimg` (AOSP libufdt) and
  [pil-squasher](https://github.com/linux-msm/pil-squasher) (firmware).

## 1. Unlock the bootloader

Unlocking **wipes the Android user data** (not the EFS or `persist`).

1. In Android, enable Developer options, then **OEM unlocking**.
2. Reboot to stock fastboot: power off, then hold **Volume Down + Power**.
3. `fastboot flashing unlock`, confirm on the phone.

## 2. Root, then back up the partitions

Reading partitions needs root. A Magisk-patched stock `boot.img` works
(boot Android once, patch the stock boot image in the Magisk app, flash it
with `fastboot flash boot`). Then, with the phone on ADB:

```
tools/backup_partitions.sh
```

It streams every partition except `userdata` to the PC and checks each one
against the device (SHA-1). Keep this backup safe and private: it contains
your IMEI. At the very least you need `persist`, `modemst1`, `modemst2`,
`fsg`, `fsc`, plus `boot` and `dtbo` (to go back to Android).

## 3. Collect your firmware

The modem, WiFi, DSP and video firmware are signed and must come from your
own phone. Put them under one tree on the PC; the kernel and the bring-up
image look them up as `qcom/qm215/cat/s22flip/...`:

| File | Where it comes from | Needed for |
|---|---|---|
| `mba.mbn` | `modem` partition (vfat): `image/mba.mbn` | modem |
| `modem.mbn` | `image/modem.mdt` + `modem.b*`, through `pil-squasher` | modem |
| `wcnss.mbn` | `image/wcnss.mdt` + `wcnss.b*`, through `pil-squasher` | WiFi, Bluetooth |
| `adsp.mbn` | `image/adsp.mdt` + `adsp.b*`, through `pil-squasher` | audio, sensors |
| `venus.mbn` | `image/venus.mdt` + `venus.b*`, through `pil-squasher` | video decoding (not working yet) |
| `WCNSS_qcom_wlan_nv.bin` | `persist` | WiFi |
| `aw881xx_acf.bin` | Android `/vendor/firmware` | loudspeaker amplifier profile |
| `cat,s22flip-ext-panel.bin` (top of the firmware tree, not under `qcom/`) | generated, see below | outer display |

```
mkdir -p firmware/lib/firmware/qcom/qm215/cat/s22flip
pil-squasher firmware/lib/firmware/qcom/qm215/cat/s22flip/modem.mbn modem/image/modem.mdt
```
(and the same for `wcnss`, `adsp`, `venus`; copy `mba.mbn` as is).

The outer display's init sequence is converted from your own stock `dtbo`
(entry 27 holds the panel node):
```
mkdtboimg dump dtbo.img -b entry
tools/mkmipidbi.py entry.27 /fragment@31/__overlay__/qcom,mdss_spi_st7789v2_qvga_cmd \
    firmware/lib/firmware/cat,s22flip-ext-panel.bin
```

The sensors also need two things from your phone, used at run time by
`tools/s22-sensord.c`: `/persist/sensors/sns.reg` (from the `persist`
backup) and the registry group table, extracted from the phone's own
`/vendor/bin/sensors.qti` with `tools/sns-reg-groups.py`.

## 4. Build and flash lk2nd

The stock bootloader cannot boot a mainline kernel directly; it boots
[lk2nd](https://github.com/bropple/lk2nd/tree/s22flip) from the `boot`
partition, and lk2nd boots Linux. lk2nd also needs a minimal `dtbo`.

```
git clone -b s22flip https://github.com/bropple/lk2nd
make -C lk2nd TOOLCHAIN_PREFIX=arm-none-eabi- lk2nd-msm8952
#   -> lk2nd/build-lk2nd-msm8952/lk2nd.img

git clone -b s22flip https://github.com/bropple/dtbo-lk2nd
make -C dtbo-lk2nd build/dtbo-cat-s22flip.img
```

From stock fastboot (power off, then Volume Down + Power):
```
fastboot flash dtbo dtbo-lk2nd/build/dtbo-cat-s22flip.img
fastboot flash boot lk2nd/build-lk2nd-msm8952/lk2nd.img
fastboot reboot
```

lk2nd shows its menu and reports `Cat S22 Flip (S22FLIP)`. From now on,
**press Volume Down while the phone boots** to get lk2nd's own fastboot,
which can `fastboot boot` test images. (lk2nd's `fastboot flash boot` writes
behind lk2nd itself, so it does not overwrite it.)

## 5. Build the kernel

The bring-up script expects the kernel's build output in `build/` and its
modules in `stage/`, both at the top of this repository:

```
git clone -b s22flip https://github.com/bropple/linux
K="make -C linux O=$PWD/build ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu-"
mkdir -p build && cp <postmarketOS msm89x7 config> build/.config
linux/scripts/kconfig/merge_config.sh -m -O build build/.config linux/arch/arm64/configs/s22flip.config
$K olddefconfig
$K -j"$(nproc)" Image.gz modules dtbs
$K INSTALL_MOD_PATH=$PWD/stage INSTALL_MOD_STRIP=1 modules_install
```

The base config is postmarketOS's `linux-postmarketos-qcom-msm89x7` config
(pmaports); `s22flip.config` adds this phone's drivers on top. The device
tree is `build/arch/arm64/boot/dts/qcom/qm215-cat-s22flip.dtb`.
`INSTALL_MOD_STRIP=1` matters: unstripped modules make the bring-up image too
big for lk2nd.

## 6. First boot, from RAM

`initramfs/build.sh` builds a busybox image that runs entirely in RAM and
never mounts the eMMC: the safest first boot. Besides the kernel it needs:

- a **static aarch64 busybox** at `tools/busybox-1.37.0/busybox` (busybox
  1.37.0, `defconfig` plus `CONFIG_STATIC=y`, built with
  `CROSS_COMPILE=aarch64-linux-gnu-`);
- the Adreno 308 microcode `qcom/a300_pm4.fw` and `qcom/a300_pfp.fw` from
  [linux-firmware](https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git),
  copied into `firmware/lib/firmware/qcom/`;
- your firmware from step 3 under `firmware/lib/firmware/`;
- `mkbootimg` (android-tools).

```
initramfs/build.sh                          # -> out/s22flip-bringup.img
fastboot boot out/s22flip-bringup.img       # from lk2nd's fastboot
```

The phone shows up as a USB network device: the host gets an address by
DHCP and the phone is `172.16.42.1`, with a telnet shell (and a serial shell
on the USB ACM port).

```
tools/s22sh 'uname -a; cat /proc/cpuinfo | head'
```

For WiFi and other tools, `tools/mkrootfs.sh` builds a small Alpine
userspace that the bring-up image fetches into RAM over USB. The modem
(`modem.mbn` is too big for the ramdisk) is fetched the same way, and its
EFS is served by `rmtfs` from RAM copies (`tools/phone/efs-to-ram.sh`,
`tools/phone/modem-up.sh`).

## 7. Install a system on the eMMC

How this project's own system is laid out, which works well with lk2nd:

```
stock bootloaders (sbl1, tz, rpm, aboot)   never touched
        |
lk2nd (boot partition)                     reads /extlinux/extlinux.conf
        |
/boot  = eMMC "cache" partition, ext2      kernel, DTB, extlinux.conf
/      = eMMC "userdata" partition          any filesystem the kernel has built in
/home  = microSD (optional)
```

- **lk2nd reads `extlinux.conf`** from any partition of 16 MiB or more that it
  can mount as **ext2**. Its ext2 reader has no extent support, so make that
  partition with `mkfs.ext2` (not ext4). The 256 MiB `cache` partition is a
  good fit; Android does not need it any more.
- `extlinux.conf` paths are relative to that partition:
  ```
  default linux
  label linux
      kernel /Image.gz
      fdt /qm215-cat-s22flip.dtb
      append root=PARTLABEL=userdata rootwait rw console=tty0 clk_ignore_unused pd_ignore_unused
  ```
- With the eMMC, its clocks and regulators and your root filesystem built
  into the kernel, no initramfs is needed: `root=PARTLABEL=userdata rootwait`
  finds the root by its GPT name.
- Build the root filesystem on the PC (any aarch64 distribution), make it
  into images, and flash them from lk2nd's fastboot: `fastboot flash cache
  boot.img` and `fastboot flash userdata root.img` (sparse images work).
- Firmware goes into the root filesystem's `/usr/lib/firmware`.
- The modem's EFS: let `rmtfs` serve copies of `modemst1`, `modemst2`, `fsc`
  and `fsg` from files (`rmtfs -o DIR -s`) instead of writing the partitions.

**Protect the rest of the eMMC from Linux.** Everything but `userdata` and
`cache` can be made read-only at boot (`blockdev --setro`), and the eMMC's
own *power-on* write protection (`mmc writeprotect user set pwron`, from
mmc-utils) can lock whole 4 MiB groups until the next reset. Use power-on
protection only; the other kinds are permanent.

## Going back to Android

From stock fastboot, flash your backed-up `dtbo` and `boot`. (Restore
`userdata`/`cache` with a factory reset if you reformatted them.)

## Help, notes, status

[HARDWARE.md](HARDWARE.md) has the status of every part of the phone and the
details: the bootloader, the displays, audio, the modem, the sensors behind
the ADSP, and what is still missing.
