# K-PHD: VM Setup & Module Testing Guide

Because testing kernel modules on your host machine is extremely dangerous (a single mistake causes a system-wide kernel panic), we use a QEMU Virtual Machine as a sandbox.

This guide walks you through:
1. Setting up an Arch Linux VM using `virt-manager` (GUI).
2. Transferring the `kphd.ko` module to the VM.
3. Safely testing the module.

---

## Part 1: Setting up the VM Sandbox (GUI Method)

Since you've already installed the `qemu` / `libvirt` packages on your Arch host, the easiest way to provision the VM is via the graphical interface.

1. **Launch Virtual Machine Manager:**
   - Open your desktop application menu and search for **Virtual Machine Manager**, OR run `virt-manager` in your terminal.
2. **Download an Arch Linux ISO:**
   - Go to [archlinux.org/download](https://archlinux.org/download/) and download the latest `.iso` file.
3. **Create the VM in virt-manager:**
   - Click the **"Create a new virtual machine"** icon (monitor with a shining star).
   - Choose **"Local install media"** and select the Arch Linux `.iso` you downloaded.
   - Assign RAM and CPU (e.g., 2048 MB RAM, 2 CPUs is plenty).
   - Create a disk image (e.g., 10 GB to 20 GB).
   - Name it `K-PHD-Test-Box` (or similar) and click **Finish**.
4. **Install Arch Linux inside the VM:**
   - The VM console will boot to the Arch installer.
   - Run the arch installation script: `archinstall`.
   - Follow the prompts to set up a basic system (make sure to set a password for `root` or a regular user, and install a network profile so the VM has internet/SSH access).
5. **Install SSH inside the VM:**
   - Once installed and rebooted, log into the VM console.
   - Install SSH: `pacman -S openssh`
   - Enable and start SSH: `systemctl enable --now sshd`
6. **Find the VM's IP address:**
   - Inside the VM, run `ip a` and look for the `eth0` or `enp1s0` interface IP (usually looks like `192.168.122.x`).

---

## Part 2: Transferring the Module

Now that the VM is running and accessible via SSH, you must send the compiled kernel module from your Host to the VM.

1. **On your HOST machine (your Arch Linux PC):**
   Ensure you are in the directory where `kphd.ko` was compiled:
   ```bash
   cd ~/Desktop/K-PHD/kernel
   ```

2. **Secure Copy (SCP) the file to the VM:**
   *(Replace `<user>` and `<vm_ip>` with the username and IP address from Part 1)*
   ```bash
   scp kphd.ko <user>@<vm_ip>:~/
   ```

---

## Part 3: Safely Testing the Kernel Module

We will now insert the kernel module into the VM's kernel safely.

1. **SSH into the VM:**
   ```bash
   ssh <user>@<vm_ip>
   ```

2. **Clear the kernel message log (optional, makes it easier to read):**
   ```bash
   sudo dmesg -c
   ```

3. **Insert the K-PHD module (`insmod`):**
   ```bash
   sudo insmod kphd.ko
   ```

4. **Verify it loaded correctly:**
   ```bash
   sudo dmesg | tail -n 5
   ```
   *✅ You should see: `K-PHD: Module loaded successfully.`*

5. **List running modules to prove it's loaded:**
   ```bash
   lsmod | grep kphd
   ```

6. **Remove the K-PHD module (`rmmod`):**
   ```bash
   sudo rmmod kphd
   ```

7. **Verify it unloaded correctly:**
   ```bash
   sudo dmesg | tail -n 5
   ```
   *✅ You should see: `K-PHD: Module unloaded successfully.`*

> **Congratulations!** Phase 1 is officially complete once you verify the `dmesg` logs. We have successfully proven that our build system works and the basic C scaffolding does not crash the kernel.
