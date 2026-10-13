# Contributing to Socketline Server

Thank you for your interest in contributing to **Socketline Server**! We hold systems code to high performance, correctness, and concurrency standards.

---

## Development Workflow

### 1. Build and Test
Always build and test using the provided CMake presets before opening a PR:

```bash
# Release build & tests
cmake --preset release
cmake --build --preset release -j
ctest --preset release --output-on-failure

# AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset asan
cmake --build --preset asan -j
./build/asan/socketline_tests

# ThreadSanitizer
cmake --preset tsan
cmake --build --preset tsan -j
./build/tsan/socketline_tests
```

### 2. Code Formatting & Linting
The codebase adheres to the Google C++ Style Guide with a 4-space indent and 100-character column limit:

```bash
# Check formatting
clang-format-18 -i --dry-run --Werror src/*.cpp include/socketline/*.hpp tests/*.cpp

# Apply formatting
clang-format-18 -i src/*.cpp include/socketline/*.hpp tests/*.cpp
```

### 3. Benchmarking Policy
Any PR touching the hot path (HTTP parsing, WebSocket framing, epoll dispatch, or ring buffer synchronization) must back claims with reproducible benchmark results:

```bash
# Run 30-second benchmark comparison
wrk -t12 -c400 -d30s --latency http://localhost:8080/
```
Include before-and-after throughput (req/sec) and P99 latency numbers in your PR description.

---

## Commit Guidelines

Follow the Conventional Commits specification:
- Use the imperative mood: `feat: add ...`, `fix: resolve ...`, `perf: optimize ...`.
- Scope commits logically: `feat(websocket): add ping/pong frame validation`.
- Never commit binaries, core dumps, or `build/` artifacts.
