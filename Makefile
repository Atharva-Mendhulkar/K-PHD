# ──────────────────────────────────────────────────────────────
#  K-PHD — Kernel-level Predictive Hang Detector
#  Top-level Makefile
# ──────────────────────────────────────────────────────────────

PREFIX   ?= /usr/local
KERN_DIR  = kernel
DAEMON_DIR = daemon
TESTS_DIR  = tests

.PHONY: all kernel daemon tests clean install uninstall help

all: kernel daemon tests
	@echo ""
	@echo "  Build complete! Run: sudo ./kphd start"
	@echo ""

kernel:
	@echo "Building kernel module..."
	$(MAKE) -C $(KERN_DIR)

daemon:
	@echo "Building daemon..."
	$(MAKE) -C $(DAEMON_DIR)

tests:
	@echo "Building stress tests..."
	$(MAKE) -C $(TESTS_DIR)

clean:
	$(MAKE) -C $(KERN_DIR) clean
	$(MAKE) -C $(DAEMON_DIR) clean
	$(MAKE) -C $(TESTS_DIR) clean

install: all
	@echo "Installing K-PHD to $(PREFIX)..."
	install -Dm755 kphd $(PREFIX)/bin/kphd
	install -Dm755 $(DAEMON_DIR)/kphd_daemon $(PREFIX)/bin/kphd_daemon
	install -Dm644 $(KERN_DIR)/kphd.ko $(PREFIX)/lib/kphd/kphd.ko
	@echo ""
	@echo "  Installed! Run: sudo kphd start"
	@echo ""

uninstall:
	rm -f $(PREFIX)/bin/kphd
	rm -f $(PREFIX)/bin/kphd_daemon
	rm -rf $(PREFIX)/lib/kphd
	rm -f /etc/systemd/system/kphd.service
	@echo "K-PHD uninstalled."

help:
	@echo "K-PHD Build System"
	@echo ""
	@echo "  make            Build everything"
	@echo "  make kernel     Build kernel module only"
	@echo "  make daemon     Build daemon only"
	@echo "  make tests      Build stress tests only"
	@echo "  make clean      Clean all build artifacts"
	@echo "  make install    Install system-wide (requires root)"
	@echo "  make uninstall  Remove from system (requires root)"
	@echo ""
