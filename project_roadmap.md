# **K-PHD: Project Development Roadmap**

This document outlines the phased development approach for the K-PHD (Kernel-level Predictive Hang Detector) project. The roadmap ensures a safe, iterative method for integrating low-overhead real-time monitoring directly into the Linux kernel while deferring complex computations to user space.

---

## **Phase 1: LKM Scaffolding**
**Objective:** Establish the foundational Loadable Kernel Module (LKM) structure safely.
- [ ] Write `module_init` and `module_exit` macros.
- [ ] Set up the `Makefile` referencing the VM's kernel headers (`/lib/modules/$(uname -r)/build`).
- [ ] Validate basic compilation.
- [ ] Test module loading (`insmod`) and unloading (`rmmod`) via the `dmesg` logs.

## **Phase 2: Hooking the Scheduler**
**Objective:** Intercept kernel scheduler events to measure nanosecond timing.
- [ ] Include `<trace/events/sched.h>`.
- [ ] Write the probe function: `probe_sched_wakeup(void *ignore, struct task_struct *p)`.
- [ ] Write the probe function: `probe_sched_switch(void *ignore, bool preempt, struct task_struct *prev, struct task_struct *next)`.
- [ ] Use `ktime_get_ns()` to capture accurate timestamps for `T_wakeup` and `T_switch`.

## **Phase 3: Data Structures & Concurrency**
**Objective:** Safely aggregate latency metrics directly within the kernel without causing freezes.
- [ ] Implement `<linux/hashtable.h>` to store process data.
- [ ] Define a `struct` containing `pid`, `wakeup_time`, and `max_latency`.
- [ ] Implement `spin_lock_irqsave()` around hash table insertions and lookups inside the probe functions to handle concurrency predictably.

## **Phase 4: Exporting the Data (Static Sync)**
**Objective:** Expose collected latency snapshots to user space on-demand.
- [ ] Create `/proc/kphd_stats` virtual file interface using `proc_create()`.
- [ ] Implement `seq_file` operations to safely iterate over the kernel hash table and readable dump output.

## **Phase 5: Netlink Integration (Dynamic Stream)**
**Objective:** Enable real-time, asynchronous alerting from the kernel out to user space.
- [ ] Define a custom Netlink family and multicast group specifically for K-PHD.
- [ ] Write a kernel function to allocate an `sk_buff` (socket buffer).
- [ ] Pack the PID and threshold-breaking latency data into the socket buffer and broadcast using `genlmsg_multicast()`.

## **Phase 6: The Userspace Daemon**
**Objective:** Process raw kernel data streams using floating-point predictive models.
- [ ] Open a Netlink socket in an external userspace daemon (C++ or Python) bound to the K-PHD multicast group.
- [ ] Implement the Exponential Moving Average (EMA) algorithm mathematically: `EMA_{t} = alpha * L_{t} + (1 - alpha) * EMA_{t-1}`.
- [ ] Apply statistical bounds to predict CPU starvation.
- [ ] Output formatted and prioritized terminal warnings to `stdout` when anomalous latency thresholds are breached.

## **Phase 7: Stress Testing and Validation**
**Objective:** Validate system performance, absolute stability, and predictive accuracy against real-world hang types.
- [ ] Write user-space CPU hogs to simulate CPU starvation conditions.
- [ ] Develop workload scripts that generate high lock contention and severe I/O stalls to block process execution.
- [ ] Verify that the K-PHD tracker correctly triggers heuristic warnings **before** typical reporting tools like `top` hit limits.
