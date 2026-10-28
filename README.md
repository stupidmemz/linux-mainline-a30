# linux-mainline-a30



## ⚠️ Read this before flashing anything

- This replaces the **boot** partition. Back up **boot**, **recovery** and **EFS** first, and keep the copies
  off the phone. EFS holds your IMEI and radio calibration and is gonna suck to lose.
- Unlocking the bootloader trips Knox and wipes the phone.
- Have a way back: TWRP and a known-good `boot.img` that your recovery can actually read.
  A USB data cable, SD card(s),USB OTG and Odin are a good safety net to have.
- Everything here is experimental. A wrong flash can leave the phone unbootable. Use at your own risk.
- I'm not responsible for breaking devices, thermonuclear war and such.
- Never share your backups: they contain your IMEI and may contain keys and passwords.
-  This project is still a work in progress, and so is this page.
- There is no stable release yet and the release date is uncertain. Pre-releases will come out
now and then, but don't expect much from them.
- English is my second language you may see mistakes.
- Some information may not always be correct i recommend double checking this as i'm writing this very late.


## What is this?

A mainline Linux bring-up (not Android's downstream 4.4 kernel) for the **Samsung Galaxy A30**
(SM-A305F/DS) and maybe other variants.

Right now it boots a real mainline kernel on the phone, shows a console on the screen, reads the
buttons and the touchscreen, talks to the eMMC, and can hand over to a root filesystem stored on the
phone. It does **not** start a desktop yet. See the tables below.

In theory any arm64 distro with a downloadable rootfs should work. So far an OpenRC rootfs
(it reaches a text login) and a systemd rootfs both start their init. None has a working desktop yet.

## Quick install (temporary instructions)

1. **Back up first.** Read the warning at the top of this page. At minimum back up your current
   `boot` partition (in TWRP: Backup, tick Boot only), and keep a copy off the phone.
2. Download the boot image (`.img`) from the **Releases** section and copy it to the phone.
3. Boot into TWRP (other recoveries may work, but they are untested) and flash it:

```
TWRP -> Install -> Install Image -> (the Linux boot image) -> select the "Boot" partition -> swipe to flash
```

4. Reboot. The phone should now start the kernel and show the pre-boot menu.

To go back to Android, flash your backed-up `boot.img` the same way (select the **Boot** partition again).

## Known issues and notes

- **The pre-boot menu may only show "Continue boot" (or just "Boot").** Tap anywhere on the screen, or
  press any button except power, to show the other options.
- **The kernel version string is not final.** Early releases such as `0.9-PRIMARY` do not show their own
  name when booting yet. The kernel still reports itself as `6.15.0-rc1-exynos7870-dirty`: it inherits that
  suffix from the base tree, and `-dirty` means it was built from a tree with uncommitted changes. It will be
  renamed.
- Releases follow the naming in [Build conventions](#build-conventions) below.

## Tested on

| | |
|---|---|
| Phone | Samsung Galaxy A30, SM-A305F/DS, 4 GB RAM / 64 GB storage |
| SoC | Exynos 7885 (the chip ID register reports `EXYNOS7885`; Samsung's device tree overlays also say `EXYNOS7904` in their compatible string) |
| Bootloader | unlocked, vbmeta patched, TWRP installed |
| Android ROM on the phone | unofficial LineageOS 23.2 (Android 16), `/data` is plain unencrypted ext4 |

Other ROMs are **untested**.

## Status

### Works

| Feature | Notes |
|---|---|
| Boot into mainline Linux | through [uniLoader](https://github.com/ivoszbg/uniLoader), which already has an A30 board file |
| Display (console) | the framebuffer the bootloader leaves at `0xec000000`, 1080x2340, via `simpledrm` |
| All 8 CPU cores | |
| All 4 GB of RAM | three memory banks, see the hardware notes |
| Buttons | volume up / volume down / power, through `gpio-keys` on `gpa1` |
| Touchscreen | Melfas MIP4 (MSS100 firmware). Needs a small patch to the mainline `melfas_mip4` driver, because this firmware reports packets in "event format 8" |
| eMMC | the DesignWare MMC controller at `0x13500000`, running at the **safe legacy speed** |
| Pre-boot menu | see below |
| Handing over to a rootfs | a root filesystem in a folder on `userdata` boots into its own init (an OpenRC rootfs reaches a text login, a systemd rootfs starts its init) |
| Power off / reboot | through the PMU |

### Doesn't work (yet)

| Feature | Why |
|---|---|
| Graphical session / desktop | the system boots to a text login, but no desktop starts. Not understood yet |
| Faster eMMC modes (high-speed, DDR, HS200, HS400) | Samsung's timing values use a different format than the mainline driver, not mapped yet |
| Display panel control (brightness, blanking) | no panel driver, the screen only works because the bootloader leaves it running |
| GPU | no driver, everything is software rendered |
| Battery / charging info | the PMIC (Samsung S2MU106, as far as I can tell) has no driver |
| Wi-Fi, Bluetooth, modem, GPS | no drivers |
| Audio, camera, sensors | no drivers |
| USB | untested |
| Suspend / sleep | untested |
| "Reboot to recovery" | the menu entry exists, but it needs bootloader reboot-reason support |

### Harmless messages you will see in the log

- `exynos-clkout ... cannot match parent device` / `error -22` (a helper the PMU creates, not needed)
- `samsung-pinctrl 11cb0000.pinctrl: irq number not available` (a GPIO-interrupt init that does not apply to this controller)
- `Failed to create device link ... wakeup-interrupt-controller`
- `efi: UEFI not found`

## How it boots

```
Samsung bootloader (S-Boot) -> boot partition = uniLoader image
  uniLoader: kernel Image + A30 device tree + built-in initramfs (the pre-boot menu)
    pre-boot menu -> (optional) Continue boot -> root filesystem in /data/linux/rootfs
```

While Linux is installed in the boot partition, Android's own kernel is **not** there, so Android cannot start.
Your Android data is untouched. To use Android again, restore the original `boot.img` through TWRP.

## The pre-boot menu

A small static C program that runs as the first process. It needs no keyboard: volume keys move,
power selects, and the touchscreen works too.

- **File explorer**: browse `/`, `/sys`, `/proc`, `/dev`, and mount a partition read-only
- **Kernel logs**: everything, or filtered (errors, touch/i2c/input, storage)
- **Test functions**: touch test, key test, display test, battery/power, input devices, I2C devices,
  interrupts, clock summary, deferred devices
- **System info**
- **Boot**: continue boot into the Linux rootfs, auto-boot on/off, check rootfs
- **Power menu**: reboot, reboot to recovery (not wired up yet), power off

## Putting a distro on it

The Linux root filesystem lives in a folder on Android's `userdata` partition (`/data/linux/rootfs`),
so there is no repartitioning. This needs `/data` to be readable from Linux, so it must **not be encrypted**.
ROMs that encrypt `/data` are untested and probably will not work.

You will need to make termux have root permissions and type `tsu` or `su`
i recommend `tsu` because it's far easier to use

```
tsu --> mkdir /data/linux/rootfs

```
Install tar with
```
pkg install tar

```
Unpack the rootfs with **real `tar` as root** (for example from Termux with root)
then put your rootfs in "Download"

```
tar -xzf /data/media/0/Download/YOURROOTFS.tar.gz -C /data/linux/rootfs --numeric-owner
```
Replace "YOURROOTFS" with the actual rootfs name

Do **not** unpack it with a file manager or an archive app. They refuse or rewrite symlinks
("dangerous symbolic links") and cannot keep root ownership, which silently breaks the rootfs.

Simply boot the kernel kernel and continue the boot.

## Hardware notes

Everything here was read from Samsung's own device tree (decompiled from the dtb/dtbo partitions)
and confirmed by running it. Board revision 3 (overlay `A30_SWA_OPEN_02`) is what I have.

| Thing | Value |
|---|---|
| Framebuffer | `0xec000000`, 1080x2340, 32-bit, 20 MB reserved |
| Memory | `0x80000000` (`0x3bc00000`), `0xc0000000` (`0x40000000`), `0x880000000` (`0x80000000`), about 3.9 GiB total |
| Reserved regions | TIMA `0xb8000000` (2 MB), sec_debug_magic `0x80000000` (4 KB), seclog `0xc0000000` (512 KB), wifi/bt `0xe9000000` (4 MB), abox `0xe9400000` (4 MB), modem `0xf5400000` (136 MB), cp ram log `0xfdc00000` (32 MB), gnss `0xffc00000` (4 MB) |
| Keys | volume up `gpa1-5` (code 115), volume down `gpa1-6` (114), power `gpa1-7` (116) |
| Touchscreen | Melfas MIP4/MSS100 on `i2c_4`, address `0x48`, interrupt `gpa0-0` (level low), supply regulator `tsp_ldo_en` on `gpg2-1` |
| eMMC | `mmc@13500000`, 8-bit, non-removable, the kernel command line needs `clk_ignore_unused` |
| Display panel controller | EA8076 (from the node names), no mainline driver |
| Reset / poweroff | PMU registers via `syscon-reboot` and `syscon-poweroff` |

### The touchscreen patch

Samsung's driver calls this layout `EVENT_FORMAT_WITH_PRESSURE_2BYTE` (format 8). It is the same as format 3,
which mainline already handles, except that the force value is two bytes, so everything after x/y sits one
byte later: `size = packet[7]`, `pressure = packet[8..9]`, `touch_major = packet[10]`, `touch_minor = packet[11]`.

## Build conventions

Every image is labelled so you always know what you are running:

- **PRIMARY**: only things that are confirmed to work
- **TEST**: anything uncertain or risky (faster eMMC, new boot behaviour, new kernel versions...)

The kernel will carry that tag in its version string.

---

## Credits

- **HenriDellal**: the `linux-exynos7885` kernel tree this is based on (`exynos7885-base`, around Linux 6.15-rc1)
- **VDavid003**: the Exynos 7885 mainline patches that tree builds on
- **ivoszbg / uniLoader** and **faveoled**, who wrote the A30 board file
- **The postmarketOS/Nura Exynos 7885 mainlining notes**, which this whole approach follows
- **Eureka kernel** (Samsung downstream source): used as a reference for the device tree, the touch protocol and the hardware wiring
- **Claude AI** For fixing bugs i cannot fix for the life of me, drivers and planning

Without them this project wouldn't exist and be possible.

##  Future version plans

- A driver for the Samsung S2MU106 PMIC (battery, charger, regulators)
- A display panel driver for the A30's AMOLED, so brightness and blanking work
- Finding out why the distro does not start a desktop
- Mapping Samsung's eMMC timing values to the mainline driver for the fast modes

## License

The kernel changes are GPL-2.0, like the kernel. The tooling and the pre-boot menu: TBD.
