#pragma once

#include <functional>
#include <unordered_map>
#include <atomic>
#include <cstdint>
#include "socketline/epoll_backend.hpp"
#include "socketline/io_uring_backend.hpp"

namespace socketline {

class EventLoop {
public:
    using EventCallback = std::function<void(uint32_t events, int fd)>;

    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void run();
    void stop();
    bool is_running() const { return running_.load(std::memory_order_acquire); }

    int add_fd(int fd, uint32_t events, EventCallback cb);
    int modify_fd(int fd, uint32_t events);
    int remove_fd(int fd);

    void set_io_backend(IoUringBackend* backend) { io_backend_ = backend; }
    IoUringBackend* io_backend() const { return io_backend_; }
    EpollBackend& epoll_backend() { return epoll_; }

private:
    EpollBackend epoll_;
    int event_fd_{-1};
    std::atomic<bool> running_{false};
    std::unordered_map<int, EventCallback> callbacks_;
    IoUringBackend* io_backend_{nullptr};
};

} // namespace socketline
