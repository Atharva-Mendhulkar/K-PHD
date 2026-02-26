#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tracepoint.h>
#include <trace/events/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("K-PHD Dev");
MODULE_DESCRIPTION("Kernel-level Predictive Hang Detector - Phase 3");
MODULE_VERSION("0.3");

/*
 * Phase 3: Data Structures & Concurrency
 *
 * Replaced the naive fixed-size array with the kernel's built-in hash table
 * (linux/hashtable.h). Each entry is dynamically allocated via kmalloc.
 *
 * Because sched_wakeup and sched_switch fire from multiple CPUs simultaneously,
 * we protect the hash table with a spinlock using spin_lock_irqsave() — the
 * only safe locking primitive inside scheduler hooks (mutexes can sleep,
 * which is illegal in this context).
 */

/* ──────────────────────────────────────────────
 *  Runtime tracepoint pointers
 * ────────────────────────────────────────────── */
static struct tracepoint *tp_sched_wakeup = NULL;
static struct tracepoint *tp_sched_switch = NULL;

/* ──────────────────────────────────────────────
 *  Hash table: 2^10 = 1024 buckets
 * ────────────────────────────────────────────── */
#define KPHD_HASH_BITS 10
static DEFINE_HASHTABLE(kphd_htable, KPHD_HASH_BITS);

/* ──────────────────────────────────────────────
 *  Spinlock for concurrent access protection
 * ────────────────────────────────────────────── */
static DEFINE_SPINLOCK(kphd_lock);

/* ──────────────────────────────────────────────
 *  Per-process tracking entry
 * ────────────────────────────────────────────── */
struct kphd_entry {
  pid_t pid;              /* Process ID (hash key) */
  u64 wakeup_ns;          /* Timestamp when task entered runqueue */
  u64 max_latency;        /* Maximum observed scheduling latency */
  struct hlist_node node; /* Hash table linkage */
};

/* ──────────────────────────────────────────────
 *  Helper: find an existing entry by PID
 *  Caller MUST hold kphd_lock.
 * ────────────────────────────────────────────── */
static struct kphd_entry *kphd_find_locked(pid_t pid) {
  struct kphd_entry *entry;

  hash_for_each_possible(kphd_htable, entry, node, pid) {
    if (entry->pid == pid)
      return entry;
  }
  return NULL;
}

/* ──────────────────────────────────────────────
 *  Probe: sched_wakeup
 *  Records T_wakeup when a task enters the runqueue.
 *  If no entry exists for this PID, allocate one.
 * ────────────────────────────────────────────── */
static void probe_sched_wakeup(void *ignore, struct task_struct *p) {
  struct kphd_entry *entry;
  unsigned long flags;

  spin_lock_irqsave(&kphd_lock, flags);

  entry = kphd_find_locked(p->pid);
  if (!entry) {
    /* Allocate a new entry (GFP_ATOMIC: can't sleep in tracepoint) */
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry) {
      spin_unlock_irqrestore(&kphd_lock, flags);
      return;
    }
    entry->pid = p->pid;
    entry->max_latency = 0;
    hash_add(kphd_htable, &entry->node, p->pid);
  }
  entry->wakeup_ns = ktime_get_ns();

  spin_unlock_irqrestore(&kphd_lock, flags);
}

/* ──────────────────────────────────────────────
 *  Probe: sched_switch
 *  Computes latency = T_switch - T_wakeup for the incoming task.
 *  Updates max_latency if this is the worst latency observed.
 * ────────────────────────────────────────────── */
static void probe_sched_switch(void *ignore, bool preempt,
                               struct task_struct *prev,
                               struct task_struct *next,
                               unsigned int prev_state) {
  struct kphd_entry *entry;
  unsigned long flags;
  u64 now, latency_ns;

  spin_lock_irqsave(&kphd_lock, flags);

  entry = kphd_find_locked(next->pid);
  if (entry && entry->wakeup_ns != 0) {
    now = ktime_get_ns();
    latency_ns = now - entry->wakeup_ns;

    /* Track the worst-case latency per process */
    if (latency_ns > entry->max_latency)
      entry->max_latency = latency_ns;

    /* Log if latency exceeds 1ms (1,000,000 ns) */
    if (latency_ns > 1000000) {
      pr_info("K-PHD: PID %d latency = %llu ns (%llu ms) [max: %llu ms]\n",
              next->pid, latency_ns, latency_ns / 1000000,
              entry->max_latency / 1000000);
    }

    entry->wakeup_ns = 0;
  }

  spin_unlock_irqrestore(&kphd_lock, flags);
}

/* ──────────────────────────────────────────────
 *  Callback for for_each_kernel_tracepoint()
 * ────────────────────────────────────────────── */
static void lookup_tracepoints(struct tracepoint *tp, void *priv) {
  if (!strcmp(tp->name, "sched_wakeup"))
    tp_sched_wakeup = tp;
  else if (!strcmp(tp->name, "sched_switch"))
    tp_sched_switch = tp;
}

/* ──────────────────────────────────────────────
 *  Module Init
 * ────────────────────────────────────────────── */
static int __init kphd_init(void) {
  int ret;

  pr_info("K-PHD: Initializing Phase 3 — Hash Table + Spinlocks\n");

  hash_init(kphd_htable);

  /* Find tracepoints at runtime */
  for_each_kernel_tracepoint(lookup_tracepoints, NULL);

  if (!tp_sched_wakeup || !tp_sched_switch) {
    pr_err("K-PHD: Could not find scheduler tracepoints!\n");
    return -ENOENT;
  }

  /* Register probes */
  ret = tracepoint_probe_register(tp_sched_wakeup, (void *)probe_sched_wakeup,
                                  NULL);
  if (ret) {
    pr_err("K-PHD: Failed to register sched_wakeup probe (err=%d)\n", ret);
    return ret;
  }

  ret = tracepoint_probe_register(tp_sched_switch, (void *)probe_sched_switch,
                                  NULL);
  if (ret) {
    pr_err("K-PHD: Failed to register sched_switch probe (err=%d)\n", ret);
    tracepoint_probe_unregister(tp_sched_wakeup, (void *)probe_sched_wakeup,
                                NULL);
    return ret;
  }

  pr_info("K-PHD: Scheduler hooks registered (hash table ready).\n");
  return 0;
}

/* ──────────────────────────────────────────────
 *  Module Exit — free all hash table entries
 * ────────────────────────────────────────────── */
static void __exit kphd_exit(void) {
  struct kphd_entry *entry;
  struct hlist_node *tmp;
  unsigned long flags;
  int bkt;

  /* Unregister probes first to stop new entries */
  if (tp_sched_switch)
    tracepoint_probe_unregister(tp_sched_switch, (void *)probe_sched_switch,
                                NULL);
  if (tp_sched_wakeup)
    tracepoint_probe_unregister(tp_sched_wakeup, (void *)probe_sched_wakeup,
                                NULL);
  tracepoint_synchronize_unregister();

  /* Free all dynamically allocated entries */
  spin_lock_irqsave(&kphd_lock, flags);
  hash_for_each_safe(kphd_htable, bkt, tmp, entry, node) {
    hash_del(&entry->node);
    kfree(entry);
  }
  spin_unlock_irqrestore(&kphd_lock, flags);

  pr_info("K-PHD: Module unloaded, all resources freed.\n");
}

module_init(kphd_init);
module_exit(kphd_exit);
