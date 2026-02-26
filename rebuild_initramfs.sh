#!/bin/bash
set -e

# This script rebuilds the initramfs from scratch.
# Run from: ~/Desktop/K-PHD/

cd "$(dirname "$0")"

echo "=== Step 1: Clean old initramfs ==="
rm -rf initramfs rootfs.cpio.gz
mkdir -p initramfs/{bin,proc,sys,dev}

echo "=== Step 2: Copy kphd.ko module ==="
cp kernel/kphd.ko initramfs/

echo "=== Step 3: Check busybox ==="
BUSYBOX=$(which busybox 2>/dev/null)
if [ -z "$BUSYBOX" ]; then
    echo "ERROR: busybox not found. Install with: sudo pacman -S busybox"
    exit 1
fi

# Check if busybox is statically linked
if ldd "$BUSYBOX" 2>/dev/null | grep -q "not a dynamic executable"; then
    echo "busybox is statically linked (good)"
    cp "$BUSYBOX" initramfs/bin/busybox
else
    echo "busybox is dynamically linked — copying required libraries..."
    cp "$BUSYBOX" initramfs/bin/busybox

    # Copy all shared libraries that busybox needs
    mkdir -p initramfs/lib initramfs/lib64
    for lib in $(ldd "$BUSYBOX" 2>/dev/null | grep -o '/[^ ]*'); do
        DEST_DIR="initramfs$(dirname "$lib")"
        mkdir -p "$DEST_DIR"
        cp "$lib" "$DEST_DIR/"
        echo "  Copied: $lib"
    done

    # Also copy the dynamic linker
    if [ -f /lib64/ld-linux-x86-64.so.2 ]; then
        mkdir -p initramfs/lib64
        cp /lib64/ld-linux-x86-64.so.2 initramfs/lib64/
    fi
fi

chmod +x initramfs/bin/busybox

echo "=== Step 4: Create init script ==="
cat > initramfs/init << 'INIT_EOF'
#!/bin/busybox sh

# Install all standard commands (ls, cat, dmesg, insmod, rmmod)
/bin/busybox --install -s /bin

# Mount necessary virtual filesystems
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo ""
echo "======================================"
echo " Welcome to the K-PHD Testing Sandbox "
echo "======================================"
echo "Commands to test your module:"
echo "  insmod kphd.ko"
echo "  dmesg | tail -n 2"
echo "  rmmod kphd"
echo ""
echo "Type 'poweroff -f' to exit the VM."
echo "======================================"
echo ""

# Drop into a shell
exec /bin/sh
INIT_EOF
chmod +x initramfs/init

echo "=== Step 5: Pack into cpio archive ==="
cd initramfs
find . -print0 | cpio --null -o -H newc 2>/dev/null | gzip > ../rootfs.cpio.gz
cd ..

echo ""
echo "=== Done! ==="
echo "Archive size: $(du -h rootfs.cpio.gz | cut -f1)"
echo ""
echo "Contents of initramfs:"
find initramfs -type f | sort
echo ""
echo "Now run:  ./run_sandbox.sh"
