# K-PHD: Development Log

> A chronological record of every action taken, command run, and decision made during the development of K-PHD.
> **Format:** `[Date] — Step — Why`

---

## Session 1 — 2026-02-26 (Host Machine Setup)

---

### Step 1: Installed Core Kernel Development Tools

**Command:**
```bash
sudo pacman -S base-devel linux-headers git
```

**Why:**
- `base-devel`: Provides `gcc`, `make`, and other essential build tools required to compile C code and kernel modules.
- `linux-headers`: Provides the kernel header files for the currently running kernel version (`6.18.7.arch1-1`). The LKM `Makefile` references `/lib/modules/$(uname -r)/build`, which points here. Without these headers, the kernel module cannot be compiled.
- `git`: Version control for managing the project source code.

**Result:** ✅ Installed successfully. `linux-headers-6.18.7.arch1-1` and `pahole-1:1.31-2` were new installs; `base-devel` and `git` were already present and reinstalled.

---

### Step 2: Attempted to Install QEMU + libvirt (VM Sandbox)

**Command:**
```bash
sudo pacman -S qemu-full libvirt virt-manager dnsmasq bridge-utils
```

**Why:**
Per the project's safety requirement (Section 5 of `Readme.md`), kernel modules must **never** be `insmod`-ed directly on the host machine. A kernel panic inside a module crashes the entire system. The workflow mandates:
- `qemu-full`: The hypervisor (full QEMU with all device emulators) to run our sandboxed VM.
- `libvirt`: A management layer over QEMU that provides a stable API for creating/managing VMs.
- `virt-manager`: A GUI front-end for libvirt (easier VM management).
- `dnsmasq`: Provides DHCP/DNS for the virtual network so the VM can get a network address.
- `bridge-utils`: Intended to provide network bridging utilities (`brctl`).

**Result:** ❌ Failed. `bridge-utils` is **not available in Arch Linux repositories** (it was dropped; `iproute2` handles bridging natively on Arch). Because pacman aborts on any missing target, **none** of the packages were installed.

---

### Step 3: Fixed QEMU + libvirt Installation

**Command:**
```bash
sudo pacman -S qemu-full libvirt virt-manager dnsmasq
```

**Why:** Removed `bridge-utils` from the command. On Arch Linux, network bridge management is handled by `iproute2` (the `ip` command), which is already installed as a core system package. `bridge-utils` / `brctl` is a legacy tool not needed here.

**Expected Result:** Installs the full VM stack.

---

### Step 4: Enable libvirt Services

**Command:**
```bash
sudo systemctl enable --now libvirtd.socket
sudo systemctl enable --now virtnetworkd.socket
```

**Why:**
- Modern Arch Linux libvirt (v9+) uses **socket-activated** daemons instead of a monolithic `libvirtd.service`. Services start on-demand when a connection arrives.
- `libvirtd.socket`: The main libvirt control socket that `virsh` and `virt-manager` connect to.
- `virtnetworkd.socket`: Manages virtual networks (e.g., the `default` NAT network that gives the VM internet access).
- Using `.socket` units instead of `.service` units is the correct modern approach on Arch.

**Result:** ✅ Success. Systemd created the necessary symlinks in `/etc/systemd/system/sockets.target.wants/`.

---

### Step 5: Add User to `libvirt` Group

**Command:**
```bash
sudo usermod -aG libvirt $(whoami)
```

**Why:**
By default, only `root` can communicate with the libvirt daemon socket. Adding the user to the `libvirt` group grants permission to manage VMs without `sudo` for every `virsh` / `virt-manager` command.

**Result:** ✅ Success. User added to group.

---

### Step 6: Verify libvirt is Running

**Command:**
```bash
virsh list --all
```

**Why:**
To confirm that the socket activation worked and that the `virsh` client can successfully talk to the libvirt daemon without `sudo` (after applying the group change).

**Result:** ✅ Success. Returned an empty list of VMs (`Id Name State`), meaning the daemon is reachable and functioning correctly.

---

---

## Session 2 — 2026-02-26 (Phase 1 Scaffolding)

---

### Step 7: Create Project Directory Structure

**Command:**
```bash
mkdir -p K-PHD/{kernel,daemon}
```

**Why:**
Per Phase 1 objectives, we need a clean workspace to separate the Kernel Module C code (`kernel/`) from the User-space floating-point predictive logic stack (`daemon/`).

**Result:** ✅ Success. Folders `kernel` and `daemon` created.

---

### Step 8: Create LKM Boilerplate (`kphd.c`) and Build System (`Makefile`)

**Command:**
Created `kernel/kphd.c` and `kernel/Makefile`.
Then ran `make` in `kernel/`:
```bash
make -C /lib/modules/6.18.7.arch1-1/build M=/home/atharva/Desktop/K-PHD/kernel modules
```

**Why:**
- `kphd.c`: Contains the absolute minimum code to register a loadable object in the kernel (`module_init()` and `module_exit()`) and macros defining License/Author.
- `Makefile`: Configured as a Kbuild script to pull the Arch Linux kernel headers installed in Session 1 and compile our `kphd.c` into a `.ko` object.

**Result:** ✅ Success. The `make` command succeeded and produced the `kphd.ko` module.

---

## Upcoming Steps

| Phase | Next Action |
|---|---|
| VM Setup | Create a lightweight Debian/Arch VM in virt-manager |
| Phase 1 | Test `insmod kphd.ko` and `rmmod kphd` inside the VM via `dmesg` |
| Phase 2 | Register `sched_wakeup` and `sched_switch` tracepoint probes |

---

*This log is updated after every significant action. Append new sessions below this line.*
