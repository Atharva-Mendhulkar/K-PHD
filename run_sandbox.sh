#!/bin/bash

# Ensure we run from the script's own directory
cd "$(dirname "$0")"

KERNEL="./vmlinuz-extracted"

# If the extracted kernel doesn't exist yet, extract it from the UKI
if [ ! -f "$KERNEL" ]; then
    echo "Extracting raw kernel from Unified Kernel Image (UKI)..."
    UKI="/boot/EFI/Linux/omarchy_linux.efi"
    if [ ! -f "$UKI" ]; then
        echo "ERROR: UKI not found at $UKI"
        exit 1
    fi
    sudo objcopy -O binary -j .linux "$UKI" "$KERNEL"
    if [ $? -ne 0 ]; then
        echo "ERROR: Failed to extract kernel from UKI."
        exit 1
    fi
    echo "Kernel extracted successfully."
fi

echo "Booting K-PHD Initramfs Sandbox..."
echo "Using kernel: $KERNEL"

qemu-system-x86_64 \
  -kernel "$KERNEL" \
  -initrd rootfs.cpio.gz \
  -m 512M \
  -append "console=ttyS0 quiet" \
  -nographic \
  -enable-kvm
