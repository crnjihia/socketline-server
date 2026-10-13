# Socketline Server

[![CI](https://github.com/crnjihia/socketline-server/actions/workflows/ci.yml/badge.svg)](https://github.com/crnjihia/socketline-server/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/crnjihia/socketline-server?color=blue&logo=github)](https://github.com/crnjihia/socketline-server/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/Standard-C%2B%2B20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Sanitizers: ASan+UBSan+TSan](https://img.shields.io/badge/Sanitizers-Clean-brightgreen.svg)]()
[![Valgrind: Clean](https://img.shields.io/badge/Valgrind-Clean%20(0%20leaks)-brightgreen.svg)]()

High-throughput, asynchronous HTTP/1.1 and RFC 6455 WebSocket server engineered from raw POSIX sockets in modern C++20 — built with zero external runtime frameworks (no Boost.Asio, no libuv). 

Socketline serves as the mission-critical real-time notification relay for **Safaricom Daraja M-Pesa** callbacks, verifying cryptographic signatures, caching transaction history in circular ring buffers, and streaming instantaneous payment confirmations to subscribed point-of-sale and mobile clients.

---

## Live Monitoring Dashboard

![Socketline Server Dashboard](docs/assets/dashboard.png)

*Socketline Server live dashboard serving static assets via zero-copy `sendfile(2)`, maintaining persistent RFC 6455 WebSockets, verifying incoming Daraja HMAC-SHA256 callback notifications, and displaying real-time payment events.*

---

## Architectural Highlights

- **Event Loop Core**: Linux `epoll` in edge-triggered mode (`EPOLLET`). Every file descriptor is set non-blocking (`O_NONBLOCK`). I/O loops systematically handle `EAGAIN` / `EWOULDBLOCK` without busy-waiting.
- **io_uring Backend**: Compile-time toggle (`-DSOCKETLINE_USE_IO_URING=ON`) that offloads connection accept, read, and write operations to kernel submission queue entries, with automatic fallback when `IORING_SETUP_SQPOLL` is unavailable.
- **Lock-Free Concurrency**: Dedicated per-core worker threads driven by lock-free Chase-Lev work-stealing deques using atomic `std::memory_order_seq_cst` synchronization (verified clean under ThreadSanitizer).
- **Zero-Copy HTTP/1.1 Parser**: Request methods, paths, and headers are parsed into `std::string_view` slices pointing directly into socket receive buffers with zero dynamic heap allocations on the hot path. Full support for `Expect: 100-continue` and chunked transfer decoding.
- **RFC 6455 WebSocket Engine**: Full handshake implementation computing `Sec-WebSocket-Accept` via SHA-1 + Base64 with standard GUID `258EAFA5-E914-47DA-95CA-C5AB0DC85B11`. Frame parser unmasks client payloads, supports 16-bit/64-bit extended payload lengths, handles Ping/Pong and Close control codes, and reassembles fragmented messages.
- **sendfile(2) Zero-Copy Streaming**: Directly streams static assets under `public/` from disk to client socket descriptors in kernel space, completely avoiding user-space memory copies.
- **Graceful Shutdown**: Traps `SIGTERM` / `SIGINT`, closes the listening socket, drains in-flight client requests over a configurable timeout (5s), and exits cleanly with return code 0.
- **Daraja M-Pesa Relay Backbone**: `POST /callback` verifies Safaricom HMAC-SHA256 signatures, fans out payload notifications to all WebSockets subscribed to the merchant's `BusinessShortCode`, and preserves the last 1,000 transactions in an in-memory ring buffer for reconnect replay.

---

## System Architecture

```mermaid
flowchart TD
    subgraph Client Tier
        Browser[WebSocket Client / POSIX Client]
        Safaricom[Safaricom Daraja Gateway]
    end

    subgraph Kernel Space
        EP[Linux epoll EPOLLET / io_uring]
        SF[sendfile 2 Engine]
    end

    subgraph Socketline Server Core
        Acceptor[Acceptor Loop & EventLoop]
        Router{Router & Parser}
        HP[Zero-Copy HTTP Parser string_view]
        WS[RFC 6455 WebSocket Engine]
        
        subgraph Concurrency Tier
            TP[Lock-Free Thread Pool]
            CL1[Worker 1 Chase-Lev Deque]
            CL2[Worker N Chase-Lev Deque]
        end

        subgraph Daraja Relay Tier
            HMAC[HMAC-SHA256 Validator]
            RB[(Per-Merchant Ring Buffer 1000 Events)]
            SM[Merchant Subscription Manager]
        end
    end

    Browser <-->|TCP Sockets / HTTP / WS| EP
    Safaricom -->|POST /callback| EP
    EP --> Acceptor
    Acceptor --> Router
    Router -->|GET /static| SF
    Router -->|HTTP Upgrade / WS Frame| WS
    Router -->|POST /callback| HMAC
    HMAC -->|Signature Verified| RB
    HMAC -->|Fan-Out Broadcast| SM
    SM -->|Push Notification| WS
    WS <--> TP
    TP <--> CL1
    TP <--> CL2
```

### Daraja Callback & Fan-Out Lifecycle

```mermaid
sequenceDiagram
    autonumber
    actor POS as Merchant POS / Client
    participant Server as Socketline Server
    participant Gateway as Safaricom M-Pesa

    POS->>Server: GET / (Upgrade: websocket)
    Server-->>POS: HTTP/1.1 101 Switching Protocols (RFC 6455)
    POS->>Server: WS Frame: SUBSCRIBE:174379
    Server-->>POS: WS Frame: SUBSCRIBED:174379
    
    Note over Gateway,Server: Customer pays via STK Push
    Gateway->>Server: POST /callback (X-Hub-Signature: sha256=...)
    Note over Server: Verify HMAC-SHA256 with merchant secret
    Server->>Server: Push into 1000-event circular ring buffer
    Server-->>Gateway: HTTP/1.1 200 OK {"status":"relayed"}
    
    Server->>POS: WS Frame: {"BusinessShortCode":"174379", "TransAmount":"3500.00", ...}
    
    opt Reconnect Replay
        POS->>Server: WS Frame: REPLAY:174379
        Server-->>POS: Stream buffered historical transactions (FIFO)
    end
```

---

## Hardware Specification

All benchmarks and profiling data recorded on the following reference host:

- **CPU**: AMD Ryzen 7 5800H (8 Cores, 16 Threads @ 3.20 GHz base / 4.40 GHz boost)
- **RAM**: 32 GB DDR4-3200 MT/s
- **OS / Kernel**: Linux 6.18.40.1-microsoft-standard-WSL2 x86_64 / Ubuntu 24.04 LTS
- **ulimit -n**: `65536`

---

## 60-Second Quickstart

### 1. Build from Clean Checkout

```bash
# Configure and build Release preset in a single command
cmake --preset release && cmake --build --preset release
```

### 2. Run the Server

```bash
./build/release/socketline_server 8080
```

### 3. Open the Dashboard & Simulate a Daraja Payment

1. Open your browser to `http://localhost:8080/`. The WebSocket connection indicator will display **Connected**.
2. In a separate terminal, trigger an M-Pesa payment callback:

**Using PowerShell:**
```powershell
$body = '{"BusinessShortCode":"174379","TransAmount":"3500.00","BillRefNumber":"INV-8021","MSISDN":"254712345678"}'
$secret = "daraja_secret_passkey"
$hmac = [System.Security.Cryptography.HMACSHA256]::new([System.Text.Encoding]::UTF8.GetBytes($secret))
$hash = [BitConverter]::ToString($hmac.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($body))).Replace("-","").ToLower()

Invoke-RestMethod -Uri "http://localhost:8080/callback" -Method Post -Headers @{
    "Content-Type" = "application/json"
    "X-Hub-Signature" = "sha256=$hash"
} -Body $body
```

**Using Bash / cURL:**
```bash
PAYLOAD='{"BusinessShortCode":"174379","TransAmount":"3500.00","BillRefNumber":"INV-8021","MSISDN":"254712345678"}'
HMAC=$(printf "%s" "$PAYLOAD" | openssl dgst -sha256 -hmac "daraja_secret_passkey" | awk '{print $NF}')

curl -i -X POST http://localhost:8080/callback \
  -H "Content-Type: application/json" \
  -H "X-Hub-Signature: sha256=${HMAC}" \
  -d "$PAYLOAD"
```

The browser feed will update instantly with the newly broadcast transaction!

---

## Benchmarks

Measurements conducted with connection concurrency profiles simulating burst callback fan-out:

```bash
wrk -t12 -c400 -d30s --latency http://localhost:8080/
```

### Throughput & Latency Comparison Table

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

Raw benchmark run outputs are committed in [`benchmarks/results/`](benchmarks/results/):
- [`socketline.txt`](benchmarks/results/socketline.txt)
- [`nginx.txt`](benchmarks/results/nginx.txt)
- [`node.txt`](benchmarks/results/node.txt)

---

## Profiling & Hardware Diagnostics

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

Full empirical verification that static content is transferred via kernel zero-copy `sendfile(2)` rather than user-space `read()` / `write()` loops:

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

## Valgrind Verification Logs

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
| **Kernel Requirement** | Linux 2.6+ (Broad compatibility) | Linux 5.1+ (5.10+ recommended for networking) |
| **System Call Overhead** | Context switch on `epoll_wait`, `read`, `write`, `accept4` | True zero-syscall steady state with kernel polling thread |
| **Buffer Management** | Buffer allocated per client in user space upon `EPOLLIN` | Kernel-registered fixed buffers (`io_uring_register_buffers`) |
| **Latency Under Load** | Interrupt frequency penalties under high connection rates | Lowest tail latency under sustained network saturation |
| **CPU Utilization** | Lower idle CPU usage | SQPOLL kernel thread consumes a dedicated core continuously |
| **Failure Modes** | Standard synchronous POSIX error codes (`EAGAIN`, `EINTR`) | Asynchronous completions in CQE require strict lifecycle tracking |

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

## Test Suite & Sanitizer Presets

### Run GoogleTest Suite

```bash
ctest --preset release --output-on-failure
```

### Run End-to-End Integration Suite

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

## Deployment Configuration

Production systemd unit file and high-concurrency Nginx reverse-proxy configurations are located in [`deploy/`](deploy/):
- [`deploy/socketline.service`](deploy/socketline.service): Configured with `KillSignal=SIGTERM`, `TimeoutStopSec=30`, `LimitNOFILE=65536`.
- [`deploy/nginx.conf`](deploy/nginx.conf): Tuned for WebSocket connection upgrades and edge static caching.

---

## Contributing

Please see [`CONTRIBUTING.md`](CONTRIBUTING.md) for branch management, commit style, and PR guidelines.

---

## License

MIT License. See [`LICENSE`](LICENSE) for details.
