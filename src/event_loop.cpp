#include "socketline/event_loop.hpp"
#include <unistd.h>
#include <sys/eventfd.h>
#include <iostream>
#include <cerrno>
#include <vector>

namespace socketline {

EventLoop::EventLoop() {
    if (!epoll_.init()) {
        std::cerr << "Failed to initialize EpollBackend" << std::endl;
        std::terminate();
    }

    event_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (event_fd_ == -1) {
        std::cerr << "Failed to create eventfd" << std::endl;
        std::terminate();
    }

    // Register eventfd with epoll for waking up loop on stop()
    if (!epoll_.add(event_fd_, EPOLLIN)) {
        std::cerr << "Failed to add eventfd to epoll" << std::endl;
        std::terminate();
    }
}

EventLoop::~EventLoop() {
    stop();
    if (event_fd_ >= 0) {
        epoll_.remove(event_fd_);
        ::close(event_fd_);
        event_fd_ = -1;
    }
}

void EventLoop::run() {
    running_.store(true, std::memory_order_release);
    constexpr int kMaxEvents = 128;
    epoll_event events[kMaxEvents];

    while (running_.load(std::memory_order_acquire)) {
        if (io_backend_ && io_backend_->is_initialized()) {
            io_backend_->process_completions();
        }

        // 100ms timeout allows io_uring completion checks even under low epoll traffic
        int timeout_ms = (io_backend_ && io_backend_->is_initialized()) ? 10 : -1;
        int n = epoll_.wait(events, kMaxEvents, timeout_ms);

        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << "epoll_wait error: " << errno << std::endl;
            break;
        }

        for (int i = 0; i < n; ++i) {
            int fd = events[i].data.fd;
            uint32_t ev = events[i].events;

            if (fd == event_fd_) {
                // Drain eventfd notification
                uint64_t val = 0;
                while (::read(event_fd_, &val, sizeof(val)) > 0) {}
                continue;
            }

            EventCallback cb;
            auto it = callbacks_.find(fd);
            if (it != callbacks_.end()) {
                cb = it->second;
            }

            if (cb) {
                try {
                    cb(ev, fd);
                } catch (const std::exception& e) {
                    std::cerr << "Callback exception on fd " << fd << ": " << e.what() << std::endl;
                }
            }
        }
    }
}

void EventLoop::stop() {
    bool expected = true;
    if (running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        if (event_fd_ >= 0) {
            uint64_t val = 1;
            ssize_t w = ::write(event_fd_, &val, sizeof(val));
            (void)w;
        }
    }
}

int EventLoop::add_fd(int fd, uint32_t events, EventCallback cb) {
    if (!epoll_.add(fd, events)) {
        return -1;
    }
    callbacks_[fd] = std::move(cb);
    return 0;
}

int EventLoop::modify_fd(int fd, uint32_t events) {
    if (!epoll_.modify(fd, events)) {
        return -1;
    }
    return 0;
}

int EventLoop::remove_fd(int fd) {
    epoll_.remove(fd);
    callbacks_.erase(fd);
    return 0;
}

} // namespace socketline
