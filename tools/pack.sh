#!/usr/bin/env bash
set -euo pipefail
cd ~/a30/uniLoader
cp ../linux/out/arch/arm64/boot/Image blob/Image
cp ../linux/out/arch/arm64/boot/dts/exynos/exynos7885-a30.dtb blob/dtb
# valid but empty placeholder ramdisk (the real initramfs is built into the kernel)
printf '' | cpio -o -H newc > blob/ramdisk
rm -f uniLoader
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j4 a30_defconfig
make ARCH=aarch64 CROSS_COMPILE=aarch64-linux-gnu- -j4
ls -l uniLoader
touch ../empty_ramdisk
mkbootimg --header_version 1 --os_version 16.0.0 --os_patch_level 2026-06 \
  --kernel uniLoader --ramdisk ../empty_ramdisk --pagesize 0x00000800 \
  --base 0x00000000 --kernel_offset 0x10008000 --ramdisk_offset 0x11000000 \
  --second_offset 0x00000000 --tags_offset 0x10000100 --out ../uni-boot.img
ls -l ../uni-boot.img ../boot.img
sha256sum ../uni-boot.img
