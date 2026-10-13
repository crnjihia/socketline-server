#!/usr/bin/env bash
# Run all benchmarks and store results in benchmarks/results/

set -e

mkdir -p results

# Build the benchmark executable (assuming CMake preset 'bench')
cmake --preset bench || true
cmake --build --preset bench || true

# Execute benchmark and capture output
if [[ -f build/bench/socketline_bench ]]; then
    ./build/bench/socketline_bench > results/benchmark_http.txt
    echo "Benchmark results saved to results/benchmark_http.txt"
else
    echo "Benchmark executable not found – please configure and build with a 'bench' preset."
fi
