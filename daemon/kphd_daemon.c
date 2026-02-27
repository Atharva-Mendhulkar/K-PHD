/*
 * K-PHD Userspace Daemon — Phase 6
 *
 * Listens on the KPHD Generic Netlink multicast group for real-time
 * scheduling latency alerts from the kernel module. Computes an
 * Exponential Moving Average (EMA) per PID to predict CPU starvation
 * before it becomes critical.
 *
 * EMA formula:  EMA_t = alpha * L_t + (1 - alpha) * EMA_{t-1}
 *
 * Build:  make  (requires libnl-3, libnl-genl-3)
 * Run:    sudo ./kphd_daemon
 */

#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>

/* ──────────────────────────────────────────────
 *  Must match the kernel module's definitions
 * ────────────────────────────────────────────── */
#define KPHD_GENL_FAMILY "KPHD"
#define KPHD_MCGRP_NAME "kphd_alerts"

enum kphd_nl_attrs {
  KPHD_A_UNSPEC,
  KPHD_A_PID,
  KPHD_A_LATENCY_NS,
  KPHD_A_MAX_LATENCY,
  __KPHD_A_MAX,
};
#define KPHD_A_MAX (__KPHD_A_MAX - 1)

enum kphd_nl_cmds {
  KPHD_C_UNSPEC,
  KPHD_C_ALERT,
  __KPHD_C_MAX,
};

/* ──────────────────────────────────────────────
 *  EMA configuration
 * ────────────────────────────────────────────── */
#define ALPHA 0.3              /* Smoothing factor (0 < alpha <= 1) */
#define WARNING_THRESH 2000000 /* 2 ms in nanoseconds */
#define DANGER_THRESH 5000000  /* 5 ms — predicted hang territory */
#define MAX_TRACKED_PIDS 4096

/* ──────────────────────────────────────────────
 *  Per-PID EMA tracker
 * ────────────────────────────────────────────── */
struct pid_tracker {
  int pid;
  double ema;          /* Current EMA value (ns) */
  double max_observed; /* Worst latency seen */
  int alert_count;     /* Number of alerts received */
  int active;          /* Slot in use? */
};

static struct pid_tracker trackers[MAX_TRACKED_PIDS];
static volatile int running = 1;

/* ──────────────────────────────────────────────
 *  ANSI color codes
 * ────────────────────────────────────────────── */
#define C_RESET "\033[0m"
#define C_RED "\033[1;31m"
#define C_YELLOW "\033[1;33m"
#define C_GREEN "\033[1;32m"
#define C_CYAN "\033[1;36m"
#define C_DIM "\033[2m"

/* ──────────────────────────────────────────────
 *  Find or allocate a tracker for a PID
 * ────────────────────────────────────────────── */
static struct pid_tracker *get_tracker(int pid) {
  int i;
  int free_slot = -1;

  for (i = 0; i < MAX_TRACKED_PIDS; i++) {
    if (trackers[i].active && trackers[i].pid == pid)
      return &trackers[i];
    if (!trackers[i].active && free_slot == -1)
      free_slot = i;
  }

  if (free_slot == -1)
    return NULL;

  trackers[free_slot].pid = pid;
  trackers[free_slot].ema = 0.0;
  trackers[free_slot].max_observed = 0.0;
  trackers[free_slot].alert_count = 0;
  trackers[free_slot].active = 1;
  return &trackers[free_slot];
}

/* ──────────────────────────────────────────────
 *  Get current timestamp string
 * ────────────────────────────────────────────── */
static const char *timestamp(void) {
  static char buf[32];
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(buf, sizeof(buf), "%H:%M:%S", t);
  return buf;
}

/* ──────────────────────────────────────────────
 *  Process an incoming Netlink alert
 * ────────────────────────────────────────────── */
static int handle_alert(struct nl_msg *msg, void *arg) {
  (void)arg;
  struct nlattr *attrs[KPHD_A_MAX + 1];
  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  uint32_t pid;
  uint64_t latency_ns;
  uint64_t max_latency; // Declare max_latency here
  struct pid_tracker *tracker;
  double latency_ms, ema_ms;
  const char *level;
  const char *color;

  /* Parse attributes from the Netlink message */
  nla_parse(attrs, KPHD_A_MAX, genlmsg_attrdata(gnlh, 0),
            genlmsg_attrlen(gnlh, 0), NULL);

  if (!attrs[KPHD_A_PID] || !attrs[KPHD_A_LATENCY_NS])
    return NL_SKIP;

  pid = nla_get_u32(attrs[KPHD_A_PID]);
  latency_ns = nla_get_u64(attrs[KPHD_A_LATENCY_NS]);
  max_latency = attrs[KPHD_A_MAX_LATENCY]
                    ? nla_get_u64(attrs[KPHD_A_MAX_LATENCY])
                    : latency_ns;
  (void)max_latency; // Cast to void to suppress unused variable warning

  /* Update EMA for this PID */
  tracker = get_tracker(pid);
  if (!tracker)
    return NL_OK;

  tracker->alert_count++;
  if ((double)latency_ns > tracker->max_observed)
    tracker->max_observed = (double)latency_ns;

  /* EMA calculation:  EMA_t = alpha * L_t + (1 - alpha) * EMA_{t-1} */
  if (tracker->ema == 0.0)
    tracker->ema = (double)latency_ns; /* First sample = seed */
  else
    tracker->ema = ALPHA * (double)latency_ns + (1.0 - ALPHA) * tracker->ema;

  latency_ms = (double)latency_ns / 1000000.0;
  ema_ms = tracker->ema / 1000000.0;

  /* Classify severity based on EMA */
  if (tracker->ema > DANGER_THRESH) {
    level = "DANGER";
    color = C_RED;
  } else if (tracker->ema > WARNING_THRESH) {
    level = "WARNING";
    color = C_YELLOW;
  } else {
    level = "INFO";
    color = C_GREEN;
  }

  /* Print alert */
  printf("%s[%s]%s %s%-7s%s PID %-6d | "
         "latency: %s%.2f ms%s | "
         "EMA: %s%.2f ms%s | "
         "max: %.2f ms | "
         "alerts: %d\n",
         C_DIM, timestamp(), C_RESET, color, level, C_RESET, pid, color,
         latency_ms, C_RESET, color, ema_ms, C_RESET,
         tracker->max_observed / 1000000.0, tracker->alert_count);

  /* Predictive warning: EMA trending toward danger */
  if (tracker->ema > DANGER_THRESH && tracker->alert_count >= 3) {
    printf("%s  ⚠ PREDICTION: PID %d is experiencing sustained "
           "CPU starvation (EMA=%.2f ms). Potential hang imminent!%s\n",
           C_RED, pid, ema_ms, C_RESET);
  }

  fflush(stdout);
  return NL_OK;
}

/* ──────────────────────────────────────────────
 *  Signal handler for clean shutdown
 * ────────────────────────────────────────────── */
static void sig_handler(int sig) {
  (void)sig;
  running = 0;
}

/* ──────────────────────────────────────────────
 *  Main
 * ────────────────────────────────────────────── */
int main(void) {
  struct nl_sock *sock;
  int family_id, grp_id;
  int ret;

  signal(SIGINT, sig_handler);
  signal(SIGTERM, sig_handler);

  printf("%s", C_CYAN);
  printf("╔════════════════════════════════════════════╗\n");
  printf("║     K-PHD Userspace Daemon v0.6            ║\n");
  printf("║     Predictive Hang Detector               ║\n");
  printf("╚════════════════════════════════════════════╝\n");
  printf("%s\n", C_RESET);
  printf("EMA alpha=%.2f | Warning=%llu ns | Danger=%llu ns\n\n", ALPHA,
         (unsigned long long)WARNING_THRESH, (unsigned long long)DANGER_THRESH);

  /* 1. Allocate Netlink socket */
  sock = nl_socket_alloc();
  if (!sock) {
    fprintf(stderr, "ERROR: Failed to allocate Netlink socket\n");
    return 1;
  }

  /* Disable sequence number checking (multicast messages have seq=0) */
  nl_socket_disable_seq_check(sock);

  /* Set message callback */
  nl_socket_modify_cb(sock, NL_CB_VALID, NL_CB_CUSTOM, handle_alert, NULL);

  /* 2. Connect to Generic Netlink */
  ret = genl_connect(sock);
  if (ret < 0) {
    fprintf(stderr, "ERROR: genl_connect failed: %s\n", nl_geterror(ret));
    nl_socket_free(sock);
    return 1;
  }

  /* 3. Resolve the KPHD family ID */
  family_id = genl_ctrl_resolve(sock, KPHD_GENL_FAMILY);
  if (family_id < 0) {
    fprintf(stderr,
            "ERROR: Could not resolve GENL family '%s'.\n"
            "Is the kphd.ko module loaded?\n",
            KPHD_GENL_FAMILY);
    nl_socket_free(sock);
    return 1;
  }
  printf("Connected to GENL family '%s' (id=%d)\n", KPHD_GENL_FAMILY,
         family_id);

  /* 4. Subscribe to the multicast group */
  grp_id = genl_ctrl_resolve_grp(sock, KPHD_GENL_FAMILY, KPHD_MCGRP_NAME);
  if (grp_id < 0) {
    fprintf(stderr, "ERROR: Could not resolve multicast group '%s'\n",
            KPHD_MCGRP_NAME);
    nl_socket_free(sock);
    return 1;
  }

  ret = nl_socket_add_membership(sock, grp_id);
  if (ret < 0) {
    fprintf(stderr, "ERROR: nl_socket_add_membership failed: %s\n",
            nl_geterror(ret));
    nl_socket_free(sock);
    return 1;
  }
  printf("Subscribed to multicast group '%s' (id=%d)\n", KPHD_MCGRP_NAME,
         grp_id);
  printf("\nListening for latency alerts...\n\n");

  /* 5. Event loop — receive and process alerts */
  while (running) {
    ret = nl_recvmsgs_default(sock);
    if (ret < 0 && ret != -NLE_AGAIN) {
      if (running)
        fprintf(stderr, "Receive error: %s\n", nl_geterror(ret));
      break;
    }
  }

  printf("\n%sK-PHD Daemon shutting down.%s\n", C_CYAN, C_RESET);

  nl_socket_drop_membership(sock, grp_id);
  nl_socket_free(sock);

  return 0;
}
