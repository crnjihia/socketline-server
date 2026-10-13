#!/usr/bin/env bash
# compare_node.sh: Reproducible benchmark comparing Socketline against Node.js
set -euo pipefail

TARGET_URL="${1:-http://localhost:8080/}"
RESULTS_DIR="$(dirname "$0")/results"
mkdir -p "$RESULTS_DIR"

echo "=== Benchmarking Node.js ==="
echo "Target: $TARGET_URL"

# Standard wrk run
wrk -t12 -c400 -d30s --latency "$TARGET_URL" | tee "$RESULTS_DIR/node.txt"

echo "Results saved to $RESULTS_DIR/node.txt"
