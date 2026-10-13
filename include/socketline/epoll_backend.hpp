#pragma once

#include <sys/epoll.h>
#include <cstdint>

namespace socketline {

class EpollBackend {
public:
    EpollBackend();
    ~EpollBackend();

    EpollBackend(const EpollBackend&) = delete;
    EpollBackend& operator=(const EpollBackend&) = delete;

    EpollBackend(EpollBackend&& other) noexcept;
    EpollBackend& operator=(EpollBackend&& other) noexcept;

    bool init();
    int fd() const { return epoll_fd_; }

    bool add(int fd, uint32_t events);
    bool modify(int fd, uint32_t events);
    bool remove(int fd);

    int wait(epoll_event* events, int max_events, int timeout_ms);

    static bool set_nonblocking(int fd);
    static bool set_reuse_addr_port(int fd);

private:
    int epoll_fd_{-1};
};

} // namespace socketline
