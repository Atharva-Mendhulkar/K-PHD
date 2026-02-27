#include <linux/hashtable.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/tracepoint.h>
#include <net/genetlink.h>
#include <trace/events/sched.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("K-PHD Dev");
MODULE_DESCRIPTION("Kernel-level Predictive Hang Detector - Phase 5");
MODULE_VERSION("0.5");

/*
 * Phase 5: Netlink Integration
 *
 * Adds a Generic Netlink (GENL) multicast channel so userspace daemons
 * can receive real-time alerts the instant a process's scheduling latency
 * exceeds the threshold. This is faster than polling /proc/kphd_stats.
 *
 * Architecture:
 *   Kernel: sched_switch probe detects high latency → packs [PID, latency_ns]
 *           into an sk_buff → broadcasts via genlmsg_multicast()
 *   User:   Daemon opens a GENL socket, joins the "kphd_alerts" multicast
 *           group, and receives alerts as they happen.
 */

/* ──────────────────────────────────────────────
 *  Netlink alert threshold (1 ms = 1,000,000 ns)
 * ────────────────────────────────────────────── */
#define KPHD_ALERT_THRESHOLD_NS 1000000ULL

/* ──────────────────────────────────────────────
 *  Generic Netlink definitions
 * ────────────────────────────────────────────── */

/* Attributes carried in each Netlink message */
enum kphd_nl_attrs {
  KPHD_A_UNSPEC,
  KPHD_A_PID,         /* u32: process ID */
  KPHD_A_LATENCY_NS,  /* u64: scheduling latency in nanoseconds */
  KPHD_A_MAX_LATENCY, /* u64: worst-case latency for this PID */
  __KPHD_A_MAX,
};
#define KPHD_A_MAX (__KPHD_A_MAX - 1)

/* Commands (we only have one: alert notification) */
enum kphd_nl_cmds {
  KPHD_C_UNSPEC,
  KPHD_C_ALERT, /* Kernel → Userspace: latency alert */
  __KPHD_C_MAX,
};

/* Attribute policy for validation */
static const struct nla_policy kphd_nl_policy[KPHD_A_MAX + 1] = {
    [KPHD_A_PID] = {.type = NLA_U32},
    [KPHD_A_LATENCY_NS] = {.type = NLA_U64},
    [KPHD_A_MAX_LATENCY] = {.type = NLA_U64},
};

/* Multicast group — userspace joins this to receive alerts */
static const struct genl_multicast_group kphd_mcgrps[] = {
    {.name = "kphd_alerts"},
};

/* Family definition (registered in kphd_init) */
static struct genl_family kphd_genl_family = {
    .name = "KPHD",
    .version = 1,
    .maxattr = KPHD_A_MAX,
    .policy = kphd_nl_policy,
    .mcgrps = kphd_mcgrps,
    .n_mcgrps = ARRAY_SIZE(kphd_mcgrps),
    .module = THIS_MODULE,
};

/* ──────────────────────────────────────────────
 *  Runtime tracepoint pointers
 * ────────────────────────────────────────────── */
static struct tracepoint *tp_sched_wakeup = NULL;
static struct tracepoint *tp_sched_switch = NULL;

/* ──────────────────────────────────────────────
 *  Hash table + spinlock
 * ────────────────────────────────────────────── */
#define KPHD_HASH_BITS 10
static DEFINE_HASHTABLE(kphd_htable, KPHD_HASH_BITS);
static DEFINE_SPINLOCK(kphd_lock);

/* /proc entry */
static struct proc_dir_entry *kphd_proc_entry;

/* Track whether GENL family is registered (for clean exit) */
static bool genl_registered = false;

/* ──────────────────────────────────────────────
 *  Per-process tracking entry
 * ────────────────────────────────────────────── */
struct kphd_entry {
  pid_t pid;
  u64 wakeup_ns;
  u64 last_latency;
  u64 max_latency;
  u64 total_latency;
  u32 sample_count;
  struct hlist_node node;
};

/* ──────────────────────────────────────────────
 *  Helper: find entry by PID (caller holds lock)
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
 *  Netlink: broadcast a latency alert
 *  Called from sched_switch probe when latency > threshold.
 *  MUST be called WITHOUT kphd_lock held (sk_buff alloc can't
 *  be done under our spinlock safely).
 * ────────────────────────────────────────────── */
static void kphd_send_alert(pid_t pid, u64 latency_ns, u64 max_latency) {
  struct sk_buff *skb;
  void *msg_head;

  skb = genlmsg_new(nla_total_size(sizeof(u32)) + nla_total_size(sizeof(u64)) +
                        nla_total_size(sizeof(u64)),
                    GFP_ATOMIC);
  if (!skb)
    return;

  msg_head = genlmsg_put(skb, 0, 0, &kphd_genl_family, 0, KPHD_C_ALERT);
  if (!msg_head) {
    nlmsg_free(skb);
    return;
  }

  /* Pack the alert data into Netlink attributes */
  nla_put_u32(skb, KPHD_A_PID, pid);
  nla_put_u64_64bit(skb, KPHD_A_LATENCY_NS, latency_ns, KPHD_A_UNSPEC);
  nla_put_u64_64bit(skb, KPHD_A_MAX_LATENCY, max_latency, KPHD_A_UNSPEC);

  genlmsg_end(skb, msg_head);

  /* Broadcast to all listeners in multicast group 0 */
  genlmsg_multicast(&kphd_genl_family, skb, 0, 0, GFP_ATOMIC);
}

/* ──────────────────────────────────────────────
 *  Probe: sched_wakeup
 * ────────────────────────────────────────────── */
static void probe_sched_wakeup(void *ignore, struct task_struct *p) {
  struct kphd_entry *entry;
  unsigned long flags;

  spin_lock_irqsave(&kphd_lock, flags);

  entry = kphd_find_locked(p->pid);
  if (!entry) {
    entry = kmalloc(sizeof(*entry), GFP_ATOMIC);
    if (!entry) {
      spin_unlock_irqrestore(&kphd_lock, flags);
      return;
    }
    entry->pid = p->pid;
    entry->max_latency = 0;
    entry->last_latency = 0;
    entry->total_latency = 0;
    entry->sample_count = 0;
    hash_add(kphd_htable, &entry->node, p->pid);
  }
  entry->wakeup_ns = ktime_get_ns();

  spin_unlock_irqrestore(&kphd_lock, flags);
}

/* ──────────────────────────────────────────────
 *  Probe: sched_switch
 * ────────────────────────────────────────────── */
static void probe_sched_switch(void *ignore, bool preempt,
                               struct task_struct *prev,
                               struct task_struct *next,
                               unsigned int prev_state) {
  struct kphd_entry *entry;
  unsigned long flags;
  u64 now, latency_ns;
  pid_t alert_pid = 0;
  u64 alert_latency = 0, alert_max = 0;

  spin_lock_irqsave(&kphd_lock, flags);

  entry = kphd_find_locked(next->pid);
  if (entry && entry->wakeup_ns != 0) {
    now = ktime_get_ns();
    latency_ns = now - entry->wakeup_ns;

    entry->last_latency = latency_ns;
    entry->total_latency += latency_ns;
    entry->sample_count++;

    if (latency_ns > entry->max_latency)
      entry->max_latency = latency_ns;

    /* Prepare alert data (send AFTER releasing lock) */
    if (latency_ns > KPHD_ALERT_THRESHOLD_NS) {
      alert_pid = next->pid;
      alert_latency = latency_ns;
      alert_max = entry->max_latency;
    }

    entry->wakeup_ns = 0;
  }

  spin_unlock_irqrestore(&kphd_lock, flags);

  /* Send Netlink alert outside the spinlock */
  if (alert_pid && genl_registered)
    kphd_send_alert(alert_pid, alert_latency, alert_max);
}

/* ──────────────────────────────────────────────
 *  /proc/kphd_stats — seq_file (from Phase 4)
 * ────────────────────────────────────────────── */
static int kphd_stats_show(struct seq_file *sf, void *v) {
  struct kphd_entry *entry;
  unsigned long flags;
  int bkt;
  u64 avg_latency;

  seq_printf(sf, "K-PHD Scheduling Latency Report\n");
  seq_printf(sf, "================================\n");
  seq_printf(sf, "%-8s %-14s %-14s %-14s %-10s\n", "PID", "LAST(ns)", "MAX(ns)",
             "AVG(ns)", "SAMPLES");
  seq_printf(
      sf, "--------------------------------------------------------------\n");

  spin_lock_irqsave(&kphd_lock, flags);
  hash_for_each(kphd_htable, bkt, entry, node) {
    if (entry->sample_count > 0) {
      avg_latency = entry->total_latency / entry->sample_count;
      seq_printf(sf, "%-8d %-14llu %-14llu %-14llu %-10u\n", entry->pid,
                 entry->last_latency, entry->max_latency, avg_latency,
                 entry->sample_count);
    }
  }
  spin_unlock_irqrestore(&kphd_lock, flags);

  seq_printf(sf, "================================\n");
  return 0;
}

static int kphd_stats_open(struct inode *inode, struct file *file) {
  return single_open(file, kphd_stats_show, NULL);
}

static const struct proc_ops kphd_proc_ops = {
    .proc_open = kphd_stats_open,
    .proc_read = seq_read,
    .proc_lseek = seq_lseek,
    .proc_release = single_release,
};

/* ──────────────────────────────────────────────
 *  Tracepoint lookup callback
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

  pr_info("K-PHD: Initializing Phase 5 — Netlink Integration\n");

  hash_init(kphd_htable);

  /* 1. Register Generic Netlink family */
  ret = genl_register_family(&kphd_genl_family);
  if (ret) {
    pr_err("K-PHD: Failed to register GENL family (err=%d)\n", ret);
    return ret;
  }
  genl_registered = true;

  /* 2. Create /proc/kphd_stats */
  kphd_proc_entry = proc_create("kphd_stats", 0444, NULL, &kphd_proc_ops);
  if (!kphd_proc_entry) {
    pr_err("K-PHD: Failed to create /proc/kphd_stats\n");
    genl_unregister_family(&kphd_genl_family);
    genl_registered = false;
    return -ENOMEM;
  }

  /* 3. Find tracepoints */
  for_each_kernel_tracepoint(lookup_tracepoints, NULL);
  if (!tp_sched_wakeup || !tp_sched_switch) {
    pr_err("K-PHD: Could not find scheduler tracepoints!\n");
    proc_remove(kphd_proc_entry);
    genl_unregister_family(&kphd_genl_family);
    genl_registered = false;
    return -ENOENT;
  }

  /* 4. Register probes */
  ret = tracepoint_probe_register(tp_sched_wakeup, (void *)probe_sched_wakeup,
                                  NULL);
  if (ret) {
    pr_err("K-PHD: Failed to register sched_wakeup (err=%d)\n", ret);
    proc_remove(kphd_proc_entry);
    genl_unregister_family(&kphd_genl_family);
    genl_registered = false;
    return ret;
  }

  ret = tracepoint_probe_register(tp_sched_switch, (void *)probe_sched_switch,
                                  NULL);
  if (ret) {
    pr_err("K-PHD: Failed to register sched_switch (err=%d)\n", ret);
    tracepoint_probe_unregister(tp_sched_wakeup, (void *)probe_sched_wakeup,
                                NULL);
    proc_remove(kphd_proc_entry);
    genl_unregister_family(&kphd_genl_family);
    genl_registered = false;
    return ret;
  }

  pr_info(
      "K-PHD: Netlink family 'KPHD' registered, /proc ready, hooks active.\n");
  return 0;
}

/* ──────────────────────────────────────────────
 *  Module Exit
 * ────────────────────────────────────────────── */
static void __exit kphd_exit(void) {
  struct kphd_entry *entry;
  struct hlist_node *tmp;
  unsigned long flags;
  int bkt;

  if (tp_sched_switch)
    tracepoint_probe_unregister(tp_sched_switch, (void *)probe_sched_switch,
                                NULL);
  if (tp_sched_wakeup)
    tracepoint_probe_unregister(tp_sched_wakeup, (void *)probe_sched_wakeup,
                                NULL);
  tracepoint_synchronize_unregister();

  proc_remove(kphd_proc_entry);

  if (genl_registered) {
    genl_unregister_family(&kphd_genl_family);
    genl_registered = false;
  }

  spin_lock_irqsave(&kphd_lock, flags);
  hash_for_each_safe(kphd_htable, bkt, tmp, entry, node) {
    hash_del(&entry->node);
    kfree(entry);
  }
  spin_unlock_irqrestore(&kphd_lock, flags);

  pr_info("K-PHD: Module unloaded, Netlink family removed, resources freed.\n");
}

module_init(kphd_init);
module_exit(kphd_exit);
