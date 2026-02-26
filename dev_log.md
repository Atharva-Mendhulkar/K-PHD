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

---

## Session 3 — 2026-02-26 (Alternative VM Sandbox)

---

### Step 9: Build Initramfs Testing Environment

**Command:**
Created `initramfs/init` script with `busybox` and created `run_sandbox.sh`.
```bash
sudo pacman -S --noconfirm busybox cpio
mkdir -p initramfs/bin
cp /usr/bin/busybox initramfs/bin/
cp kernel/kphd.ko initramfs/
# (Created init script to mount /proc /sys /dev)
cd initramfs && find . | cpio -o -H newc | gzip > ../rootfs.cpio.gz
```

**Why:**
The user requested a faster, easier alternative to downloading an ISO and installing Arch Linux via `virt-manager`.
We constructed an **initramfs** (Initial RAM Filesystem) that packs our compiled `kphd.ko` and a minimal `busybox` shell into a 2MB archive (`rootfs.cpio.gz`). QEMU can load this archive directly into RAM and boot using the host's existing `/boot/vmlinuz-linux` kernel in under 3 seconds.

This provides the exact same safety guarantees (ring-0 isolation) as a full VM without the overhead.

**Result:** ✅ Success. The `rootfs.cpio.gz` archive and `run_sandbox.sh` launcher were generated.

---

### Step 10: Resolved Kernel Boot Issue (Limine + UKI)

**Problem:**
The system uses the **Limine bootloader** with **Unified Kernel Images (UKI)**. The kernel is bundled inside `/boot/EFI/Linux/omarchy_linux.efi` rather than the standard `/boot/vmlinuz-linux`. QEMU's `-kernel` flag cannot boot a UKI directly.

**Solution:**
Extracted the raw kernel image from the UKI using:
```bash
sudo objcopy -O binary -j .linux /boot/EFI/Linux/omarchy_linux.efi vmlinuz-extracted
```

**Why:**
A UKI is a PE executable that bundles the kernel, initramfs, and boot parameters into a single `.efi` file. `objcopy -j .linux` extracts just the raw kernel binary (the `.linux` section), which QEMU can then use with its `-kernel` flag.

**Result:** ✅ QEMU successfully booted kernel `6.18.7-arch1-1`.

---

### Step 11: Phase 1 Verification — `insmod` / `rmmod` Test

**Command (inside QEMU sandbox):**
```bash
insmod kphd.ko
dmesg | tail -n 2
rmmod kphd
dmesg | tail -n 2
```

**Why:**
To prove the LKM scaffolding is correct and that the module can safely load into and unload from a running Linux kernel without crashing.

**Result:** ✅ **Phase 1 Complete.**
```
[   91.059020] kphd: module verification failed: signature and/or required key missing
[   91.061876] K-PHD: Module loaded successfully.
[  106.891348] K-PHD: Module unloaded successfully.
```
The "module verification failed" warning is expected — it indicates the module isn't cryptographically signed, which is normal for development. The module loaded and unloaded without any kernel panic.

---

---

## Session 4 — 2026-02-26 (Phase 2: Scheduler Hooks)

---

### Step 12: Implemented Scheduler Tracepoint Hooks

**Changes to `kphd.c`:**
- Added `probe_sched_wakeup()` — records `ktime_get_ns()` when a task enters the runqueue.
- Added `probe_sched_switch()` — computes `latency = T_switch - T_wakeup` for the incoming task. Logs any latency > 1ms.
- Used `for_each_kernel_tracepoint()` + `tracepoint_probe_register()` for runtime tracepoint discovery (kernel 6.18 does not export tracepoint symbols to out-of-tree modules).

**Why `for_each_kernel_tracepoint()` instead of `register_trace_sched_*`:**
The convenience macros (`register_trace_sched_wakeup`, etc.) reference linker symbols (`__tracepoint_sched_wakeup`) that modern kernels do not export to LKMs. The runtime discovery approach iterates all kernel tracepoints by name and stores pointers, then registers probes manually.

**Compilation issues resolved:**
1. `sched_switch` signature mismatch — kernel 6.18 added `unsigned int prev_state` parameter.
2. `__tracepoint_sched_*` undefined symbols — switched to runtime tracepoint lookup.

**Result:** ✅ **Phase 2 Complete.**
```
K-PHD: Initializing Phase 2 — Scheduler Hooks
K-PHD: Scheduler hooks registered successfully.
```
No latency warnings appeared because the sandbox has minimal scheduling contention (expected).

---

## Upcoming Steps

---

## Session 5 — 2026-02-26 (Phase 3: Data Structures & Concurrency)

---

### Step 13: Implemented Kernel Hash Table and Spinlock Protection

**Changes to `kphd.c`:**
- Replaced fixed `wakeup_table[1024]` array with `DEFINE_HASHTABLE(kphd_htable, 10)` (1024 buckets, O(1) lookup).
- Each new PID is dynamically allocated via `kmalloc(GFP_ATOMIC)` (cannot sleep inside scheduler hooks).
- All hash table access is protected by `spin_lock_irqsave(&kphd_lock, flags)` for multi-core safety.
- Added `max_latency` tracking per process.
- Module exit frees all entries via `hash_for_each_safe()` + `kfree()`.

**Result:** ✅ **Phase 3 Complete.**
```
K-PHD: Initializing Phase 3 — Hash Table + Spinlocks
K-PHD: Scheduler hooks registered (hash table ready).
K-PHD: Module unloaded, all resources freed.
```

---

## Upcoming Steps

| Phase | Next Action |
|---|---|
| Phase 4 | Create `/proc/kphd_stats` via `proc_create()` |
| Phase 4 | Implement `seq_file` operations to dump hash table |
| Phase 5 | Define Netlink family for real-time alerts |

---

*This log is updated after every significant action. Append new sessions below this line.*
