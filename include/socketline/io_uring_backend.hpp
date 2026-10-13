#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <sys/types.h>

#if SOCKETLINE_USE_IO_URING
#include <liburing.h>
#endif

namespace socketline {

class IoUringBackend {
public:
    IoUringBackend();
    ~IoUringBackend();

    IoUringBackend(const IoUringBackend&) = delete;
    IoUringBackend& operator=(const IoUringBackend&) = delete;

    IoUringBackend(IoUringBackend&& other) noexcept;
    IoUringBackend& operator=(IoUringBackend&& other) noexcept;

    bool setup(unsigned entries = 256);
    bool is_initialized() const { return initialized_; }
    bool is_sqpoll() const { return sqpoll_active_; }

    bool submit_accept(int listen_fd, std::function<void(int)> cb);
    bool submit_read(int fd, void* buf, size_t len, off_t offset, std::function<void(ssize_t)> cb);
    bool submit_write(int fd, const void* buf, size_t len, off_t offset, std::function<void(ssize_t)> cb);

    int process_completions();

private:
    struct Operation {
        enum class Type { Accept, Read, Write };
        Type type;
        std::function<void(ssize_t)> callback;
    };

    bool initialized_{false};
    bool sqpoll_active_{false};

#if SOCKETLINE_USE_IO_URING
    struct io_uring ring_{};
#endif
};

} // namespace socketline
