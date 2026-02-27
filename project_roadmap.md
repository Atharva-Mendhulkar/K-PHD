# **K-PHD: Project Development Roadmap**

This document outlines the phased development approach for the K-PHD (Kernel-level Predictive Hang Detector) project. All 7 phases have been completed and validated.

---

## **Phase 1: LKM Scaffolding** ✅
**Objective:** Establish the foundational Loadable Kernel Module (LKM) structure safely.
- [x] Write `module_init` and `module_exit` macros.
- [x] Set up the `Makefile` referencing the kernel headers (`/lib/modules/$(uname -r)/build`).
- [x] Validate basic compilation.
- [x] Test module loading (`insmod`) and unloading (`rmmod`) via the `dmesg` logs.
- [x] Set up QEMU initramfs sandbox for safe kernel module testing.

## **Phase 2: Hooking the Scheduler** ✅
**Objective:** Intercept kernel scheduler events to measure nanosecond timing.
- [x] Include `<trace/events/sched.h>`.
- [x] Write the probe function: `probe_sched_wakeup(void *ignore, struct task_struct *p)`.
- [x] Write the probe function: `probe_sched_switch(void *ignore, bool preempt, struct task_struct *prev, struct task_struct *next)`.
- [x] Use `ktime_get_ns()` to capture accurate timestamps for `T_wakeup` and `T_switch`.
- [x] Runtime tracepoint discovery via `for_each_kernel_tracepoint()`.

## **Phase 3: Data Structures & Concurrency** ✅
**Objective:** Safely aggregate latency metrics directly within the kernel without causing freezes.
- [x] Implement `<linux/hashtable.h>` with 1024 buckets to store process data.
- [x] Define `kphd_entry` struct containing `pid`, `wakeup_ns`, `last_latency`, `max_latency`, `total_latency`, `sample_count`.
- [x] Implement `spin_lock_irqsave()` around all hash table operations.
- [x] Dynamic `kmalloc(GFP_ATOMIC)` allocation per new PID.
- [x] Clean memory freeing via `hash_for_each_safe()` + `kfree()` on module exit.

## **Phase 4: Exporting the Data (Static Sync)** ✅
**Objective:** Expose collected latency snapshots to user space on-demand.
- [x] Create `/proc/kphd_stats` virtual file interface using `proc_create()` with 0444 permissions.
- [x] Implement `seq_file` operations (`kphd_stats_show`, `single_open`, `seq_read`) with formatted table output.
- [x] Display per-PID: last latency, max latency, average latency, and sample count.

## **Phase 5: Netlink Integration (Dynamic Stream)** ✅
**Objective:** Enable real-time, asynchronous alerting from the kernel out to user space.
- [x] Define Generic Netlink family `KPHD` with multicast group `kphd_alerts`.
- [x] Implement `kphd_send_alert()` to allocate `sk_buff` and pack `[PID, latency_ns, max_latency]`.
- [x] Broadcast alerts via `genlmsg_multicast()` when latency exceeds 1ms threshold.
- [x] Alert sent outside spinlock to prevent deadlocks.
- [x] Proper `genl_unregister_family()` cleanup on module exit.

## **Phase 6: The Userspace Daemon** ✅
**Objective:** Process raw kernel data streams using floating-point predictive models.
- [x] Implemented C daemon (`kphd_daemon.c`) using `libnl-genl-3.0`.
- [x] Connects to GENL family `KPHD`, subscribes to `kphd_alerts` multicast group.
- [x] Implements Exponential Moving Average: `EMA_t = 0.3 * L_t + 0.7 * EMA_{t-1}`.
- [x] Three severity levels: INFO (<2ms), WARNING (2-5ms), DANGER (>5ms).
- [x] Predicts CPU starvation when EMA > 5ms for 3+ consecutive alerts.
- [x] Colorized ANSI terminal output with timestamps.

## **Phase 7: Stress Testing and Validation** ✅
**Objective:** Validate system performance, stability, and predictive accuracy.
- [x] `cpu_hog.c` — Spawns N threads in tight CPU loops to starve the scheduler.
- [x] `io_stall.c` — Combines fsync storms with mutex contention for I/O stalls.
- [x] `run_validation.sh` — End-to-end test: load → stress → capture → validate → unload.
- [x] Validated K-PHD detects 21ms+ scheduling delays caused by CPU starvation.
- [x] Confirmed detection of high-latency processes before traditional tools (`top`).

## **Deployment** ✅
- [x] `kphd` CLI tool with interactive shell mode (14 commands).
- [x] Top-level `Makefile` with `make && sudo make install`.
- [x] Systemd service for auto-start on boot.
- [x] Auto sudo-elevation in interactive mode.
