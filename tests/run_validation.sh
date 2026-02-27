#!/bin/bash
#
# K-PHD Validation Script
#
# Runs a complete end-to-end test of the K-PHD system:
#   1. Loads the kernel module
#   2. Runs stress tests
#   3. Captures /proc/kphd_stats output
#   4. Validates that latency spikes were detected
#
# Usage: sudo ./run_validation.sh
#
# IMPORTANT: Must be run as root (insmod requires it)

set -e

# Colors
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
KERNEL_DIR="$SCRIPT_DIR/../kernel"
TESTS_DIR="$SCRIPT_DIR"
RESULTS_FILE="$SCRIPT_DIR/validation_results.txt"

echo ""
echo -e "${CYAN}╔════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║     K-PHD Full Validation Suite            ║${NC}"
echo -e "${CYAN}╚════════════════════════════════════════════╝${NC}"
echo ""

# ─── Step 1: Check prerequisites ───
echo -e "${YELLOW}[Step 1] Checking prerequisites...${NC}"

if [ "$(id -u)" -ne 0 ]; then
    echo -e "${RED}ERROR: Must be run as root (need insmod).${NC}"
    echo "Usage: sudo ./run_validation.sh"
    exit 1
fi

if [ ! -f "$KERNEL_DIR/kphd.ko" ]; then
    echo -e "${RED}ERROR: kphd.ko not found. Run 'make' in kernel/ first.${NC}"
    exit 1
fi

echo -e "${GREEN}  ✓ Prerequisites OK${NC}"

# ─── Step 2: Build stress tests ───
echo -e "${YELLOW}[Step 2] Building stress tests...${NC}"

gcc -Wall -O2 -pthread -o "$TESTS_DIR/cpu_hog" "$TESTS_DIR/cpu_hog.c"
echo -e "${GREEN}  ✓ cpu_hog compiled${NC}"

gcc -Wall -O2 -pthread -o "$TESTS_DIR/io_stall" "$TESTS_DIR/io_stall.c"
echo -e "${GREEN}  ✓ io_stall compiled${NC}"

# ─── Step 3: Load kernel module ───
echo -e "${YELLOW}[Step 3] Loading kphd.ko...${NC}"

# Remove if already loaded
rmmod kphd 2>/dev/null || true

insmod "$KERNEL_DIR/kphd.ko"
echo -e "${GREEN}  ✓ Module loaded${NC}"
dmesg | tail -n 2

# ─── Step 4: Baseline snapshot ───
echo -e "${YELLOW}[Step 4] Taking baseline snapshot...${NC}"
echo "=== BASELINE ===" > "$RESULTS_FILE"
cat /proc/kphd_stats >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"
echo -e "${GREEN}  ✓ Baseline captured${NC}"

# ─── Step 5: Run CPU Hog Test ───
echo ""
echo -e "${RED}[Step 5] Running CPU Hog Test (8 threads, 5 seconds)...${NC}"
"$TESTS_DIR/cpu_hog" 8 5
echo ""
echo "=== AFTER CPU HOG ===" >> "$RESULTS_FILE"
cat /proc/kphd_stats >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# ─── Step 6: Run IO Stall Test ───
echo -e "${YELLOW}[Step 6] Running IO Stall Test (4 workers, 5 seconds)...${NC}"
"$TESTS_DIR/io_stall" 4 5
echo ""
echo "=== AFTER IO STALL ===" >> "$RESULTS_FILE"
cat /proc/kphd_stats >> "$RESULTS_FILE"
echo "" >> "$RESULTS_FILE"

# ─── Step 7: Final Results ───
echo -e "${CYAN}[Step 7] Final K-PHD Latency Report:${NC}"
echo ""
cat /proc/kphd_stats
echo ""

# ─── Step 8: Validate Detection ───
echo -e "${YELLOW}[Step 8] Validating detection...${NC}"

# Check if any latency > 1ms was recorded
HIGH_LATENCY=$(cat /proc/kphd_stats | awk 'NR>4 && NF>=4 {if ($3 > 1000000) print $0}')

if [ -n "$HIGH_LATENCY" ]; then
    echo -e "${GREEN}  ✓ K-PHD detected high-latency processes:${NC}"
    echo "$HIGH_LATENCY" | while read -r line; do
        echo -e "    ${RED}→ $line${NC}"
    done
    echo "VALIDATION: PASS" >> "$RESULTS_FILE"
else
    echo -e "${YELLOW}  ⚠ No latencies > 1ms detected (system may be too fast)${NC}"
    echo "VALIDATION: NO HIGH LATENCY DETECTED" >> "$RESULTS_FILE"
fi

# ─── Step 9: Check dmesg for alerts ───
echo ""
echo -e "${YELLOW}[Step 9] dmesg K-PHD alerts:${NC}"
ALERTS=$(dmesg | grep "K-PHD:" | tail -n 10)
if [ -n "$ALERTS" ]; then
    echo "$ALERTS"
    echo "" >> "$RESULTS_FILE"
    echo "=== DMESG ALERTS ===" >> "$RESULTS_FILE"
    echo "$ALERTS" >> "$RESULTS_FILE"
else
    echo "  (no threshold-breaking alerts in dmesg)"
fi

# ─── Step 10: Unload module ───
echo ""
echo -e "${YELLOW}[Step 10] Unloading module...${NC}"
rmmod kphd
echo -e "${GREEN}  ✓ Module removed cleanly${NC}"
dmesg | tail -n 1

# ─── Done ───
echo ""
echo -e "${CYAN}══════════════════════════════════════════════${NC}"
echo -e "${GREEN}  Validation complete! Results saved to:${NC}"
echo -e "${GREEN}  $RESULTS_FILE${NC}"
echo -e "${CYAN}══════════════════════════════════════════════${NC}"
echo ""
