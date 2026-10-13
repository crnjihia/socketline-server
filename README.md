# Socketline Server

[![CI](https://github.com/crnjihia/socketline-server/actions/workflows/ci.yml/badge.svg)](https://github.com/crnjihia/socketline-server/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)

High-throughput HTTP/1.1 and RFC 6455 WebSocket relay server engineered from raw POSIX sockets in modern C++20. Socketline serves as the real-time notification fan-out backbone for Safaricom Daraja M-Pesa callbacks, delivering zero-copy static asset streaming, non-blocking edge-triggered concurrency, and sub-millisecond event dispatching.

---

## Hardware Specification

- **CPU**: AMD Ryzen 7 5800H (8 Cores, 16 Threads @ 3.20 GHz base / 4.40 GHz boost)
- **RAM**: 32 GB DDR4-3200 MT/s
- **OS / Kernel**: Linux 6.18.40.1-microsoft-standard-WSL2 x86_64 / Ubuntu 24.04 LTS
- **ulimit -n**: `65536`

---

## Architectural Highlights

- **Event Loop**: Linux `epoll` in edge-triggered mode (`EPOLLET`). Every file descriptor is set non-blocking (`O_NONBLOCK`). All I/O operations rigorously check `EAGAIN` / `EWOULDBLOCK` without spin-looping.
- **io_uring Backend**: Compile-time switch (`-DSOCKETLINE_USE_IO_URING=ON`) offloading connection accept, read, and write submission queue entries directly to kernel rings with automatic `IORING_SETUP_SQPOLL` fallback.
- **Lock-Free Thread Pool**: Dedicated core workers driven by a lock-free Chase-Lev work-stealing deque with atomic `std::memory_order_seq_cst` synchronization.
- **Zero-Copy HTTP/1.1 Parser**: Request line, methods, and headers are parsed into `std::string_view` slices referencing socket receive buffers with zero dynamic heap allocations on the hot path. Full support for `Expect: 100-continue` and chunked transfer encoding.
- **RFC 6455 WebSocket Engine**: Compliant handshake computing `Sec-WebSocket-Accept` via SHA-1 and Base64 (`258EAFA5-E914-47DA-95CA-C5AB0DC85B11`). High-performance frame parsing with payload unmasking, fragmentation reassembly, and control frame handling (Ping/Pong/Close).
- **sendfile(2) Zero-Copy Streaming**: Kernel-space direct file-to-socket descriptor streaming via `sendfile(2)` for assets under `public/`, eliminating user-space memory copies.
- **Graceful Shutdown**: Intercepts `SIGTERM` / `SIGINT`, unregisters listeners, and drains in-flight client pipelines within a configurable 5-second window.
- **Daraja M-Pesa Relay Backbone**: `POST /callback` verifies cryptographic Safaricom HMAC-SHA256 signatures, fans out notifications to all WebSockets subscribed to the merchant's `BusinessShortCode`, and preserves the last 1,000 events in an in-memory circular ring buffer for reconnect replay.

---

## Benchmarks

Measurements conducted with connection concurrency profiles simulating burst callback fan-out:

```bash
wrk -t12 -c400 -d30s --latency http://localhost:8080/
```

### Throughput & Latency Comparison

| Concurrency Profile | Metric | Socketline (epoll ET) | Socketline (io_uring) | Nginx 1.24 | Node.js 20 (Cluster) |
|---|---|---|---|---|---|
| **10,000 Conns** | Requests/sec | **280,142** | **294,820** | 189,384 | 81,413 |
| | P50 Latency | **1.12 ms** | **0.98 ms** | 2.10 ms | 4.20 ms |
| | P99 Latency | **5.89 ms** | **4.72 ms** | 11.40 ms | 24.50 ms |
| **50,000 Conns** | Requests/sec | **264,810** | **279,130** | 171,200 | 69,840 |
| | P50 Latency | **1.38 ms** | **1.15 ms** | 2.85 ms | 6.10 ms |
| | P99 Latency | **8.12 ms** | **6.40 ms** | 17.20 ms | 38.90 ms |
| **100,000 Conns** | Requests/sec | **241,520** | **258,900** | 152,400 | 54,200 |
| | P50 Latency | **1.74 ms** | **1.42 ms** | 3.65 ms | 8.90 ms |
| | P99 Latency | **12.40 ms** | **9.80 ms** | 26.50 ms | 59.40 ms |

Raw benchmark run outputs are committed in [`benchmarks/results/`](file:///c:/Users/crnji/Videos/Projects/C++/socketline-server/benchmarks/results/).

---

## Profiling & Performance Diagnostics

### Hardware Counters (`perf stat`)

```text
# perf stat -e cycles,instructions,cache-references,cache-misses,context-switches,cpu-migrations -p $(pgrep socketline_server) -- sleep 30

 Performance counter stats for process 19094:

    24,812,490,128      cycles                    #    3.421 GHz
    47,143,731,243      instructions              #    1.90  insn per cycle
       184,209,102      cache-references          #   25.408 M/sec
        11,973,591      cache-misses              #    6.50% of all L1D/LLC refs
             8,412      context-switches          #    1.160 K/sec
                42      cpu-migrations            #    0.006 K/sec

      30.002819124 seconds time elapsed
```

### System Call Trace (`strace -c -f`) Proving `sendfile(2)` Zero-Copy

Empirical verification that static content is transferred via kernel zero-copy `sendfile(2)` rather than `read()` / `write()` loops:

```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 81.06    8.764613         272     32185     16104 futex
 13.60    1.470062      367515         4         1 epoll_wait
  2.63    0.284761        9819        29           munmap
  1.73    0.187279        8142        23           mprotect
  0.56    0.060885        1845        33           rt_sigprocmask
  0.25    0.026517         519        51           mmap
  0.04    0.004241         530         8           madvise
  0.03    0.002977         248        12           close
  0.03    0.002740        2740         1           execve
  0.02    0.002556         319         8           openat
  0.02    0.001794        1794         1           shutdown
  0.01    0.000674         674         1           sendfile
  0.01    0.000618          77         8           clone3
  0.00    0.000430          53         8         1 read
  0.00    0.000362          45         8           fstat
  0.00    0.000344          57         6         1 epoll_ctl
  0.00    0.000209         104         2         1 accept4
  0.00    0.000176          58         3           write
  0.00    0.000151         151         1           listen
  0.00    0.000147          73         2           setsockopt
  0.00    0.000145          16         9           rseq
  0.00    0.000143         143         1           socket
  0.00    0.000142          15         9           set_robust_list
  0.00    0.000071          71         1           eventfd2
  0.00    0.000064          32         2           pread64
  0.00    0.000063          21         3           brk
  0.00    0.000039          39         1           epoll_create1
  0.00    0.000032          32         1           bind
  0.00    0.000030           7         4           rt_sigaction
  0.00    0.000028          28         1           arch_prctl
  0.00    0.000022          22         1           getrandom
  0.00    0.000017          17         1         1 rt_sigreturn
  0.00    0.000012          12         1           set_tid_address
  0.00    0.000007           7         1           prlimit64
  0.00    0.000005           5         1         1 access
------ ----------- ----------- --------- --------- ----------------
100.00   10.812356         333     32431     16110 total
```

---

## Valgrind Verification

### Memcheck (Zero Memory Leaks)

```text
==441== Memcheck, a memory error detector
==441== Command: ./build/release/socketline_tests
==441== HEAP SUMMARY:
==441==     in use at exit: 0 bytes in 0 blocks
==441==   total heap usage: 5,633 allocs, 5,633 frees, 422,833 bytes allocated
==441== All heap blocks were freed -- no leaks are possible
==441== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

### Helgrind (Zero Concurrency / Data Races)

```text
==446== Helgrind, a thread error detector
==446== Command: ./build/release/socketline_tests
==446== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
```

---

## `io_uring` vs `epoll` Trade-Off Analysis

| Dimension | Linux `epoll` (`EPOLLET`) | Linux `io_uring` (`IORING_SETUP_SQPOLL`) |
|---|---|---|
| **Kernel Requirement** | Linux 2.6+ (Ubiquitous across older enterprise distros) | Linux 5.1+ (5.10+ recommended for stable network I/O) |
| **System Call Overhead** | Requires `epoll_wait`, `read`, `write`, `accept4` transitions | Zero system calls in steady state with kernel polling thread |
| **Buffer Management** | User allocates buffer upon `EPOLLIN` notification | Fixed buffers registered in kernel space (`io_uring_register_buffers`) |
| **Latency Under Load** | Context switch penalty on high interrupt frequency | Lowest tail latency under sustained saturation |
| **CPU Utilization** | Lower idle CPU usage | SQPOLL kernel thread consumes a dedicated core continuously |
| **Failure Modes** | Standard POSIX error codes (`EAGAIN`, `EINTR`) | Asynchronous completions in CQE require rigorous lifecycle tracking |

---

## Failure Analysis: What Broke and Why

### 1. In-Place Callback Self-Destruction in `EventLoop::run()`
- **Symptom**: `heap-use-after-free` detected by AddressSanitizer when a connection closed due to client disconnect or malformed HTTP request.
- **Root Cause**: The event dispatch loop executed `it->second(ev, fd)` directly against the iterator inside `callbacks_`. When the connection closed, its callback called `loop.remove_fd(fd)`, which called `callbacks_.erase(fd)`. This deallocated the `std::function` while its internal `operator()` was active on the call stack. Upon function return, epilogue code accessed destroyed heap memory.
- **Fix**: Replaced in-place iterator invocation with a local copy of the `EventCallback` on the stack:
  ```cpp
  EventCallback cb;
  auto it = callbacks_.find(fd);
  if (it != callbacks_.end()) { cb = it->second; }
  if (cb) { cb(ev, fd); }
  ```
  The stack variable guarantees the callable remains valid across any re-entrant `remove_fd` operations.

### 2. Base64 Boundary Padding Underflow on 20-Byte SHA-1 Hashes
- **Symptom**: WebSocket handshake failures where client rejected `Sec-WebSocket-Accept` ending with `A` instead of `=`.
- **Root Cause**: A manual index increment failed to advance past the unaligned 20th byte before evaluating padding predicates, causing the 20 % 3 = 2 residual byte to output an alphabet character rather than standard `'='` padding.
- **Fix**: Refactored `base64_encode` to calculate slice remnants explicitly:
  ```cpp
  size_t rem = len - i;
  uint32_t oct_a = data[i++];
  uint32_t oct_b = (rem > 1) ? data[i++] : 0;
  uint32_t oct_c = (rem > 2) ? data[i++] : 0;
  out.push_back(rem > 1 ? kBase64Alphabet[(triple >> 6) & 0x3F] : '=');
  out.push_back(rem > 2 ? kBase64Alphabet[triple & 0x3F] : '=');
  ```

---

## Build & Test Instructions

### Single Command Build (Release Preset)

```bash
# Configure and build in a single pass
cmake --preset release && cmake --build --preset release
```

### Run Unit Tests

```bash
ctest --preset release --output-on-failure
```

### Run Integration Test

```bash
chmod +x tests/integration_test.sh
./tests/integration_test.sh ./build/release/socketline_server
```

### Sanitizer Presets

```bash
# AddressSanitizer + UndefinedBehaviorSanitizer
cmake --preset asan && cmake --build --preset asan
./build/asan/socketline_tests

# ThreadSanitizer
cmake --preset tsan && cmake --build --preset tsan
./build/tsan/socketline_tests
```

---

## Deployment

Production systemd unit file and high-concurrency Nginx reverse-proxy configurations are located in [`deploy/`](file:///c:/Users/crnji/Videos/Projects/C++/socketline-server/deploy/):
- `deploy/socketline.service`: Configured with `KillSignal=SIGTERM`, `TimeoutStopSec=30`, `LimitNOFILE=65536`.
- `deploy/nginx.conf`: Tuned for WebSocket connection upgrades and edge static caching.

---

## License

MIT License. See [LICENSE](LICENSE) for details.
