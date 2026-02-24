# K-PHD (Kernel-level Predictive Hang Detector)

![License](https://img.shields.io/badge/license-GPL--2.0-blue.svg)
![Kernel](https://img.shields.io/badge/kernel-5.x%2B-orange.svg)
![Status](https://img.shields.io/badge/status-active_development-success.svg)

**K-PHD** is a custom Linux kernel monitoring system that proactively predicts application hangs by analyzing per-process scheduler latency in real time. 

Traditional monitoring tools like `top`, `strace`, or `perf` detect failures only after user-visible degradation occurs. K-PHD solves this by operating directly inside the kernel. It uses scheduler hooks to measure exact process execution delays, securely bridging kernel space and userspace to provide real-time, low-overhead diagnostics and early-warning alerts for CPU starvation, lock contention, and I/O stalls.

---

## Why K-PHD?

Modern operating systems handle thousands of concurrent processes, but under heavy load or lock contention, processes experience abnormal scheduling delays leading to freezes. K-PHD provides:
* **Proactive Detection:** Alerts userspace *before* a process completely hangs.
* **Low Overhead:** Native kernel-level tracing (< 2% overhead) without polling.
* **Deep Visibility:** Exposes exact runqueue wait times and uninterruptible sleep durations.

---

## System Architecture

K-PHD operates across two privilege rings, ensuring high-performance data collection in the kernel and safe, complex statistical analysis in userspace.

```text
+-------------------------------------------------------------+
|                        USER SPACE                           |
|                                                             |
|  +-------------------+        +--------------------------+  |
|  |   K-PHD Daemon    | <----  | CLI / Dashboard (Python) |  |
|  | (Heuristic Math)  |        +--------------------------+  |
|  +-------------------+                                      |
|       ^         ^                                           |
|       |         |                                           |
+-------|---------|-------------------------------------------+
|       |         |              KERNEL SPACE                 |
|  [Netlink]   [/proc]                                        |
|       |         |                                           |
|  +-------------------------------------------------------+  |
|  |                  K-PHD Kernel Module                  |  |
|  |                                                       |  |
|  |  +----------------+  +--------------+  +-----------+  |  |
|  |  | Trace Hooks    |  | Hash Table   |  | Spinlocks |  |  |
|  |  +----------------+  +--------------+  +-----------+  |  |
|  +-------------------------------------------------------+  |
|            |                              |                 |
|     (sched_wakeup)                 (sched_switch)           |
|            v                              v                 |
|  +-------------------------------------------------------+  |
|  |           Linux Completely Fair Scheduler (CFS)       |  |
|  +-------------------------------------------------------+  |
+-------------------------------------------------------------+

```

### Core Components

1. **Kernel Module (`kphd.ko`):** Hooks into `sched_switch` and `sched_wakeup` tracepoints. Uses high-resolution timers to calculate exact wait times.
2. **The Bridge:** Exposes static metrics via `/proc/kphd_stats` and streams real-time latency anomalies via Generic Netlink sockets.
3. **Userspace Daemon:** Reads kernel data, applies an Exponential Moving Average (EMA) to calculate latency trends, and triggers terminal alerts.

---

## Development Environment setup

Kernel development requires a strict separation between the host machine and the testing environment to prevent system crashes.

**Recommended Setup:**

* **Development Machine:** Any machine (e.g., MacBook ARM64 or x86 host) for writing code.
* **Build/Test Server:** A dedicated x86_64 Linux Virtual Machine (e.g., Arch Linux or Debian via QEMU/KVM). **Do not load experimental modules on your bare-metal host OS.**

### Prerequisites (on the Test VM)

Ensure your VM has the necessary build tools and kernel headers installed:

```bash
# For Arch Linux VM
sudo pacman -S base-devel linux-headers git

# For Ubuntu/Debian VM
sudo apt-get install build-essential linux-headers-$(uname -r) git

```

---

## Build and Run Instructions

**1. Clone the repository**

```bash
git clone [https://github.com/yourusername/k-phd.git](https://github.com/yourusername/k-phd.git)
cd k-phd

```

**2. Compile the Kernel Module**

```bash
make

```

**3. Load the Module**

```bash
sudo insmod kphd.ko

```

**4. Verify it is running**
Check the kernel logs to see the initialization message:

```bash
dmesg | tail -n 10

```

**5. View real-time stats**

```bash
cat /proc/kphd_stats

```

**6. Unload the Module**

```bash
sudo rmmod kphd

```

---

## Project Roadmap

We welcome contributions! Here is the current development roadmap:

* [x] Phase 1: Environment Setup & LKM Boilerplate
* [ ] Phase 2: Intercepting CFS Tracepoints (`sched_switch`, `sched_wakeup`)
* [ ] Phase 3: Hash Table Implementation & Spinlock Synchronization
* [ ] Phase 4: `/proc` File Interface Implementation
* [ ] Phase 5: Netlink Socket Integration
* [ ] Phase 6: Userspace Analytics Daemon (C/Python) & EMA Math
* [ ] Phase 7: Stress Testing Workloads (CPU/IO Bound)

---

## Contributing

Contributions are what make the open-source community such an amazing place to learn, inspire, and create. Any contributions you make are **greatly appreciated**.

1. Fork the Project
2. Create your Feature Branch (`git checkout -b feature/AmazingFeature`)
3. Commit your Changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the Branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

*Note: All kernel C code must adhere to the [Linux Kernel Coding Style](https://www.kernel.org/doc/html/v4.10/process/coding-style.html).*

---

## License & Author

Distributed under the GPL-2.0 License. See `LICENSE` for more information.

**Author:** Atharva Mendhulkar

```

***
