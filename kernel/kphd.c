#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("K-PHD Dev");
MODULE_DESCRIPTION("Kernel-level Predictive Hang Detector - Phase 1");
MODULE_VERSION("0.1");

static int __init kphd_init(void) {
    pr_info("K-PHD: Module loaded successfully.\n");
    return 0; // Return 0 to indicate success
}

static void __exit kphd_exit(void) {
    pr_info("K-PHD: Module unloaded successfully.\n");
}

module_init(kphd_init);
module_exit(kphd_exit);
