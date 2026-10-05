# OpenRC services for an installed system

Services for a Linux system installed on the phone's eMMC (see step 7 of
[GETTING-STARTED.md](../../GETTING-STARTED.md)), written for OpenRC. Copy
`init.d/*` to `/etc/init.d/` and `conf.d/*` to `/etc/conf.d/`, then enable
what you want with `rc-update add NAME RUNLEVEL`.

| Service | Runlevel | What it does | Needs |
|---|---|---|---|
| `s22-partguard` | sysinit | Makes every eMMC partition except `userdata` and `cache` read-only, and puts the eMMC's own power-on write protection (cleared at the next reset) on everything that holds no byte of those two | `../s22-partguard`, mmc-utils, util-linux |
| `s22-zram` | boot | Swap on compressed RAM | zram in the kernel |
| `s22-clock` | boot | The PMIC RTC keeps time but Linux cannot set it: keeps an offset to it, saved once chrony has synced, so the time is right at boot without a network | chrony (optional, to save the offset) |
| `s22-usbnet` | default | USB network gadget (NCM): the phone is 172.16.42.1 | libcomposite; a DHCP server such as dnsmasq on `usb0` for the host |
| `rmtfs` | default | Serves the modem's EFS from files in `/var/lib/rmtfs`, seeded once by reading the EFS partitions, and starts the modem | [rmtfs](https://github.com/linux-msm/rmtfs) (`-o` directory mode), [qrtr](https://github.com/linux-msm/qrtr) |
| `s22-sensord` | default | Sensors through the ADSP (`../s22-sensord.c`) | your own `sns.reg` and group table in `/var/lib/s22-sensord/` |
| `s22-t9d` | default | Keypad text input (`../s22-t9d.c`, see [KEYPAD.md](../../docs/KEYPAD.md)) | uinput |
| `s22-lidd` | default | Main display off while the lid is closed (`../s22-lidd.c`), lid hooks in `/etc/s22-lidd.d` (`../s22-lidd.d/`); tell elogind to ignore the lid (`HandleLidSwitch=ignore`) | |
| `s22-outerd` | default | Outer display status screen while the lid is closed (`../s22-outerd/`) | `s22-outer` |
| `tqftpserv` | default | TFTP over QRTR for the modem's MCFG files, before `rmtfs` | [tqftpserv](https://github.com/linux-msm/tqftpserv), `modem_pr` next to the modem firmware |
| `s22-outer` | default | Outer display in a known state (black, backlight off): the built-in panel driver probes before the root filesystem holds its init sequence, so this rebinds it | `cat,s22flip-ext-panel.bin` in `/usr/lib/firmware` |

The C tools build with a plain `gcc -O2 -o NAME NAME.c` on the phone, or
`aarch64-linux-gnu-gcc` on a PC.
