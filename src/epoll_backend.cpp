#include "socketline/epoll_backend.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <utility>

namespace socketline {

EpollBackend::EpollBackend() = default;

EpollBackend::~EpollBackend() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

EpollBackend::EpollBackend(EpollBackend&& other) noexcept
    : epoll_fd_(other.epoll_fd_) {
    other.epoll_fd_ = -1;
}

EpollBackend& EpollBackend::operator=(EpollBackend&& other) noexcept {
    if (this != &other) {
        if (epoll_fd_ >= 0) {
            ::close(epoll_fd_);
        }
        epoll_fd_ = other.epoll_fd_;
        other.epoll_fd_ = -1;
    }
    return *this;
}

bool EpollBackend::init() {
    if (epoll_fd_ >= 0) {
        ::close(epoll_fd_);
    }
    epoll_fd_ = ::epoll_create1(EPOLL_CLOEXEC);
    return epoll_fd_ >= 0;
}

bool EpollBackend::add(int fd, uint32_t events) {
    if (epoll_fd_ < 0) return false;
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) == 0;
}

bool EpollBackend::modify(int fd, uint32_t events) {
    if (epoll_fd_ < 0) return false;
    epoll_event ev{};
    ev.events = events;
    ev.data.fd = fd;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev) == 0;
}

bool EpollBackend::remove(int fd) {
    if (epoll_fd_ < 0) return false;
    return ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int EpollBackend::wait(epoll_event* events, int max_events, int timeout_ms) {
    if (epoll_fd_ < 0) return -1;
    return ::epoll_wait(epoll_fd_, events, max_events, timeout_ms);
}

bool EpollBackend::set_nonblocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool EpollBackend::set_reuse_addr_port(int fd) {
    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        return false;
    }
#ifdef SO_REUSEPORT
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) < 0) {
        return false;
    }
#endif
    return true;
}

} // namespace socketline
