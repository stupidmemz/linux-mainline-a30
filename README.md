# linux-mainline-exynos7885

The (probably) first mainline Linux port to devices nobody cares about

⚠️ this project is still WIP, same with this page

release date is uncertain until it reaches a stable state

pre-releases will probably come out soon, don't expect much

HORRIBLE english incoming (spare my life)

## Notes
early uploaded versions such as 0.9-PRIMARY are not named when booted yet

it reports itself as linux-6.15-rc1-exynos7870-dirty

there were other stuff at the name but its pretty long and i dont wanna bother checking it
as it will be renamed anyway

theres a known bug where the preboot menu only shows Continue boot or just Boot

press anywhere on the screen or any button (except power) to show the other options

---
(temp) How to Install
You simply download the boot image from the "Releases" section
In TWRP do this (other recoveries may work but they are untested)

```
TWRP --> Install --> Install image --> (The linux boot image) --> Boot
```
And simply swipe to Flash

Reboot and it should boot the kernel


---

## What is this?

A mainline Linux (not Android's downstream 4.4 kernel) bring-up for the **Samsung Galaxy A30**
(SM-A305F/DS) and, hopefully one day, the rest of the Exynos 7885 family (A10, A20, A20e, A30s, A40...).

Right now it boots a real mainline kernel on the phone, shows a console on the screen, reads the
buttons and the touchscreen, talks to the eMMC, and can hand over to a root filesystem stored on the
phone. It does **not** yet start a desktop. See the tables below.

## Tested on

| | |
|---|---|
| Phone | Samsung Galaxy A30, SM-A305F/DS, 4 GB RAM / 64 GB storage |
| SoC | Exynos 7885 (the chip ID register reports `EXYNOS7885`; Samsung's device tree overlays also say `EXYNOS7904` in their compatible string) |
| Bootloader | unlocked, vbmeta patched, TWRP installed |
| Android ROM on the phone | unofficial LineageOS 23.2 (Android 16), `/data` is plain unencrypted ext4 |

Other Exynos 7885 phones and other ROMs are **untested**.

## Status

### Works

| Feature | Notes |
|---|---|
| Boot into mainline Linux | through [uniLoader](https://github.com/ivoszbg/uniLoader), which already has an A30 board file |
| Display (console) | the framebuffer the bootloader leaves at `0xec000000`, 1080x2340, via `simpledrm` |
| All 8 CPU cores | |
| All 4 GB of RAM | three memory banks, see the hardware notes |
| Buttons | volume up / volume down / power, through `gpio-keys` on `gpa1` |
| Touchscreen | Melfas MIP4 (MSS100 firmware). Needs a one-line-ish patch to the mainline `melfas_mip4` driver: this firmware reports packet "event format 8" |
| eMMC | the DesignWare MMC controller at `0x13500000`, running at the **safe legacy speed** |
| Pre-boot menu (TUI) | see below |
| Handing over to a rootfs | a root filesystem in a folder on `userdata` boots into its own init (OpenRC and systemd both reach a login prompt) |
| Power off / reboot | through the PMU |

### Doesn't work (yet)

| Feature | Why |
|---|---|
| A graphical session / desktop | the system boots to a text login but no desktop starts. Not understood yet |
| Faster eMMC modes (high-speed, DDR, HS200, HS400) | timing values from Samsung's tree use a different format than the mainline driver, not mapped yet |
| Display panel control (brightness, blanking) | no panel driver, the screen only works because the bootloader leaves it running |
| GPU | no driver, everything is software rendered |
| Battery / charging info | the PMIC (Samsung S2MU106 as far as I can tell) has no driver |
| Wi-Fi, Bluetooth, modem, GPS | no drivers |
| Audio, camera, sensors | no drivers |
| USB | untested |
| Suspend / sleep | untested |
| "Reboot to recovery" | the menu entry exists but needs bootloader reboot-reason support |

### Harmless messages you will see in the log

- `exynos-clkout ... cannot match parent device` / `error -22` (a helper the PMU creates, not needed)
- `samsung-pinctrl 11cb0000.pinctrl: irq number not available` (a GPIO-interrupt init that does not apply to this controller)
- `Failed to create device link ... wakeup-interrupt-controller`
- `efi: UEFI not found`

## How it boots

```
Samsung bootloader -> boot partition = uniLoader image
  uniLoader: kernel Image + A30 device tree + built-in initramfs (the pre-boot menu)
    pre-boot menu -> (optional) Continue boot -> root filesystem in /data/linux/rootfs
```

Android's own kernel is **not** in the boot partition while Linux is installed there.
To get Android back you restore the original `boot.img` through TWRP
(Install Image > `boot.img` > Boot).

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

The Linux root filesystem lives in a folder on Android's `userdata` partition
(`/data/linux/rootfs`), so there is no repartitioning, and Android keeps working. This needs `/data` to be
readable from Linux, i.e. **not encrypted**. ROMs that encrypt `/data` are untested and probably will not work.

Unpack the rootfs with **real `tar` as root** (for example from Termux with root):

```
tar -xpf rootfs.tar.gz -C /data/linux/rootfs --numeric-owner
```

Do **not** unpack it with a file manager or archive app. They refuse or rewrite the symlinks
("dangerous symbolic links") and cannot keep root ownership, which silently breaks the rootfs.

## Hardware notes

Everything here was read from Samsung's own device tree (decompiled from the dtb/dtbo partitions)
and confirmed by running it. Revision 3 of the board (overlay `A30_SWA_OPEN_02`) is what I have.

| Thing | Value |
|---|---|
| Framebuffer | `0xec000000`, 1080x2340, 32-bit, 20 MB reserved |
| Memory | `0x80000000` (`0x3bc00000`), `0xc0000000` (`0x40000000`), `0x880000000` (`0x80000000`), about 3.9 GiB total |
| Reserved regions | TIMA `0xb8000000` (2 MB), sec_debug_magic `0x80000000` (4 KB), seclog `0xc0000000` (512 KB), wifi/bt `0xe9000000` (4 MB), abox `0xe9400000` (4 MB), modem `0xf5400000` (136 MB), cp ram log `0xfdc00000` (32 MB), gnss `0xffc00000` (4 MB) |
| Keys | volume up `gpa1-5` (code 115), volume down `gpa1-6` (114), power `gpa1-7` (116) |
| Touchscreen | Melfas MIP4/MSS100 on `i2c_4`, address `0x48`, interrupt `gpa0-0` (level low), supply regulator `tsp_ldo_en` on `gpg2-1` |
| eMMC | `mmc@13500000`, 8-bit, non-removable, kernel command line needs `clk_ignore_unused` |
| Display panel controller | EA8076 (from the node names), no mainline driver |
| Reset / poweroff | PMU registers via `syscon-reboot` and `syscon-poweroff` |

### The touchscreen patch

Samsung's driver calls this layout `EVENT_FORMAT_WITH_PRESSURE_2BYTE` (format 8). It is the same as format 3,
which mainline handles, except the force value is two bytes, so everything after x/y sits one byte later:
`size = packet[7]`, `pressure = packet[8..9]`, `touch_major = packet[10]`, `touch_minor = packet[11]`.

## Build conventions

Every image is labelled so you always know what you are running:

- **PRIMARY**: only things that are confirmed to work
- **TEST**: anything uncertain or risky (faster eMMC, new boot behaviour, new kernel versions...)

The kernel will carry that tag in its version string.

## ⚠️ Read this before flashing anything

- This replaces the **boot** partition. Back up **boot**, **recovery** and **EFS** first, and keep the copies
  off the phone. EFS holds your IMEI and radio calibration and is painful to lose.
- Unlocking the bootloader trips Knox and wipes the phone.
- Have a way back: TWRP and a known-good `boot.img` that your recovery can actually read.
  A USB data cable plus Odin is a good safety net to have .
- Everything here is experimental. A wrong flash can leave the phone unbootable. Use at your own risk.
- Never commit or share backups: they contain your IMEI and may contain keys and passwords.

## Credits

- **HenriDellal**: the `linux-exynos7885` kernel tree this is based on (`exynos7885-base`, around Linux 6.15-rc1)
- **VDavid003**: the Exynos 7885 mainline patches that tree builds on
- **ivoszbg / uniLoader** and **faveoled**, who wrote the A30 board file
- **The postmarketOS Exynos 7885 mainlining notes**, which this whole approach follows
- **Eureka kernel** (Samsung downstream source): used as a reference for the device tree, the touch protocol and the hardware wiring

## Help wanted

- A driver for the Samsung S2MU106 PMIC (battery, charger, regulators)
- A display panel driver for the A30's AMOLED, so brightness and blanking work
- Figuring out why the distro does not start a desktop
- Mapping Samsung's eMMC timing values to the mainline driver for the fast modes
- Testing on other Exynos 7885 phones

## License

The kernel changes are GPL-2.0, like the kernel. The tooling and the pre-boot menu: TBD.
