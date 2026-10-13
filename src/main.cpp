#include "socketline/event_loop.hpp"
#include "socketline/epoll_backend.hpp"
#include "socketline/io_uring_backend.hpp"
#include "socketline/thread_pool.hpp"
#include "socketline/connection.hpp"
#include "socketline/daraja_relay.hpp"

#include <iostream>
#include <memory>
#include <unordered_map>
#include <csignal>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cerrno>
#include <chrono>
#include <thread>

using namespace socketline;

static std::atomic<bool> g_shutdown_requested{false};
static EventLoop* g_loop = nullptr;

static void sigterm_handler(int signum) {
    (void)signum;
    g_shutdown_requested.store(true, std::memory_order_release);
    if (g_loop) {
        g_loop->stop();
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: socketline_server [port]\n"
                      << "High-throughput C++20 HTTP/1.1 and WebSocket relay for Safaricom Daraja M-Pesa.\n";
            return 0;
        }
        try {
            port = std::stoi(arg);
        } catch (...) {
            std::cerr << "Invalid port: " << arg << "\n";
            return 1;
        }
    }

    std::cout << "Starting Socketline Server on port " << port << "...\n";

    // Setup signal handling for graceful shutdown
    struct sigaction sa{};
    sa.sa_handler = sigterm_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    // Initialize ThreadPool (one thread per core)
    ThreadPool thread_pool;
    std::cout << "Initialized work-stealing thread pool with " << thread_pool.size() << " workers.\n";

    // Initialize Daraja relay backbone
    const char* env_secret = std::getenv("DARAJA_SECRET");
    std::string secret = env_secret ? env_secret : "daraja_secret_passkey";
    DarajaRelay daraja_relay(secret);

    // Initialize EventLoop
    EventLoop loop;
    g_loop = &loop;

#if SOCKETLINE_USE_IO_URING
    IoUringBackend io_backend;
    if (io_backend.setup(512)) {
        std::cout << "io_uring backend enabled (SQPOLL="
                  << (io_backend.is_sqpoll() ? "active" : "inactive") << ")\n";
        loop.set_io_backend(&io_backend);
    } else {
        std::cerr << "io_uring setup failed; falling back to epoll.\n";
    }
#endif

    // Create listening socket
    int listen_fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listen_fd < 0) {
        perror("socket");
        return 1;
    }

    if (!EpollBackend::set_reuse_addr_port(listen_fd)) {
        perror("set_reuse_addr_port");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind");
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        ::close(listen_fd);
        return 1;
    }

    std::unordered_map<int, std::unique_ptr<Connection>> connections;

    // Register listener on epoll with EPOLLET
    loop.add_fd(listen_fd, EPOLLIN | EPOLLET, [&](uint32_t, int) {
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = ::accept4(listen_fd,
                                      reinterpret_cast<sockaddr*>(&client_addr),
                                      &client_len,
                                      SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (client_fd < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break; // All pending connections accepted
                }
                if (errno == EINTR) continue;
                perror("accept4");
                break;
            }

            auto conn = std::make_unique<Connection>(client_fd, &thread_pool, &daraja_relay);
            int cfd = client_fd;

            loop.add_fd(cfd, EPOLLIN | EPOLLET | EPOLLRDHUP, [&loop, &connections, cfd](uint32_t ev, int) {
                auto it = connections.find(cfd);
                if (it != connections.end()) {
                    auto* conn = it->second.get();
                    conn->on_event(ev);
                    if (conn->is_closed()) {
                        loop.remove_fd(cfd);
                        connections.erase(cfd);
                    }
                }
            });

            connections[cfd] = std::move(conn);
        }
    });

    std::cout << "Accepting connections on 0.0.0.0:" << port << " [epoll ET mode]...\n";
    loop.run();

    // -------------------------------------------------------------
    // Graceful Shutdown Sequence
    // -------------------------------------------------------------
    std::cout << "\nSIGTERM received: initiating graceful shutdown drain...\n";

    // 1. Stop accepting new connections
    loop.remove_fd(listen_fd);
    ::close(listen_fd);

    // 2. Drain in-flight requests with configurable timeout (5s)
    auto drain_start = std::chrono::steady_clock::now();
    constexpr auto kDrainTimeout = std::chrono::seconds(5);

    while (!connections.empty() &&
           (std::chrono::steady_clock::now() - drain_start) < kDrainTimeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 3. Close any remaining connections
    for (auto& [fd, conn] : connections) {
        if (conn && !conn->is_closed()) {
            loop.remove_fd(fd);
            conn->close_connection();
        }
    }
    connections.clear();

    // 4. Shutdown worker threads
    thread_pool.stop();

    std::cout << "Socketline server drained and shut down cleanly.\n";
    return 0;
}
