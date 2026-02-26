# K-PHD: Easier Alternatives to VM Setup

Setting up a full Virtual Machine with an ISO can be tedious. Because we only need a safe place to run `insmod` and `rmmod` without crashing your host, there are much faster, automated ways to achieve this.

Here are the best two alternatives, ranked from easiest to slightly more involved.

---

## Alternative 1: Use a Pre-built Cloud Image (Easiest)

Instead of installing an OS from scratch, we can download a ready-to-run Linux Cloud Image. These bypass the installation screens entirely.

### Step 1: Download the Alpine Linux Virtual Image
Alpine Linux is extremely lightweight (only ~60MB) and boots in seconds.
```bash
cd ~/Desktop/K-PHD
wget https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-virt-3.19.1-x86_64.iso
```

*(Note: While Alpine is great for normal software, loading custom Kernel Modules usually requires matching kernel headers. If our module compiled against Arch Linux headers, it might complain when loaded into an Alpine kernel.)*

### Step 2: Download an Arch Linux Cloud Image (Better Compatibility)
Since your host is Arch, using an Arch guest guarantees the kernel versions will perfectly match the headers we compiled against.

```bash
cd ~/Desktop/K-PHD
# Download the Arch Linux qcow2 cloud image
wget https://geo.mirror.pkgbuild.com/images/latest/Arch-Linux-x86_64-cloudimg.qcow2
```

### Step 3: Run it instantly with QEMU
You don't need `virt-manager` or libvirt for this. You can launch it straight from your terminal:
```bash
qemu-system-x86_64 \
  -m 2G \
  -smp 2 \
  -enable-kvm \
  -nographic \
  -drive file=Arch-Linux-x86_64-cloudimg.qcow2,format=qcow2
```
*Note: Cloud images require "cloud-init" configured to set a default password. If you don't feel like creating a cloud-init iso, Alternative 2 is better.*

---

## Alternative 2: The "Initramfs" Method (The Fastest Developer Workflow)

This is how kernel hackers rapidly test modules. We skip the hard drive and the OS entirely. We just boot your **existing Arch Linux kernel directly into QEMU** using a tiny temporary filesystem (initramfs). 

This takes 3 seconds to boot and Drops you directly into a root `#` shell.

### Step 1: Create a tiny folder for our "OS"
```bash
cd ~/Desktop/K-PHD
mkdir initramfs
cd initramfs
```

### Step 2: Copy your module and `busybox`
We need `busybox` to provide terminal commands (like `ls`, `insmod`, `dmesg`).
```bash
# Copy your compiled kernel module
cp ../kernel/kphd.ko ./

# Install busybox on your host if you don't have it
sudo pacman -S busybox

# Copy busybox into our tiny OS
mkdir bin
cp /usr/bin/busybox bin/
```

### Step 3: Create a minimal `init` script
When the kernel boots, it runs `/init`. We'll tell it to mount standard directories and drop us into a shell.
Create a file named `init` in the `initramfs` folder:
```bash
#!/bin/busybox sh

# Install all standard commands (ls, cat, dmesg, insmod)
/bin/busybox --install -s /bin

# Mount necessary virtual filesystems
mount -t proc none /proc
mount -t sysfs none /sys
mount -t devtmpfs none /dev

echo "======================================"
echo " Welcome to the K-PHD Testing Sandbox "
echo "======================================"

# Drop into a shell!
exec /bin/sh
```
Make the script executable:
```bash
chmod +x init
```

### Step 4: Pack the folder into an initramfs archive
```bash
find . | cpio -o -H newc | gzip > ../rootfs.cpio.gz
cd ..
```

### Step 5: Boot it!
Now, we tell QEMU to boot using your host's actual kernel (`/boot/vmlinuz-linux`), but instead of loading your real hard drive, it loads our tiny `rootfs.cpio.gz` into RAM.

```bash
qemu-system-x86_64 \
  -kernel /boot/vmlinuz-linux \
  -initrd rootfs.cpio.gz \
  -m 512M \
  -append "console=ttyS0 quiet" \
  -nographic \
  -enable-kvm
```

### Inside the Sandbox:
You will instantly see a root prompt `#`.
Now you can safely test the module:
```sh
# insmod kphd.ko
# dmesg | tail -n 2
# rmmod kphd
# dmesg | tail -n 2

# Type 'poweroff -f' to exit the sandbox
```

### Why Alternative 2 is the best:
- No ISO downloads.
- No OS installations.
- Takes 500MB of RAM and boots in 2 seconds.
- Uses your *actual* host kernel, meaning `kphd.ko` represents a 100% perfect match without version conflicts.

**Would you like me to automatically run the scripts to set up Alternative 2 for you right now?**
