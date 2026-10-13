#!/usr/bin/env bash
# compare_nginx.sh: Reproducible benchmark comparing Socketline against Nginx
set -euo pipefail

TARGET_URL="${1:-http://localhost:8080/}"
RESULTS_DIR="$(dirname "$0")/results"
mkdir -p "$RESULTS_DIR"

echo "=== Benchmarking Nginx ==="
echo "Target: $TARGET_URL"

# Standard wrk run
wrk -t12 -c400 -d30s --latency "$TARGET_URL" | tee "$RESULTS_DIR/nginx.txt"

echo "Results saved to $RESULTS_DIR/nginx.txt"
