#include "socketline/io_uring_backend.hpp"
#include <unistd.h>
#include <iostream>

namespace socketline {

#if SOCKETLINE_USE_IO_URING

IoUringBackend::IoUringBackend() = default;

IoUringBackend::~IoUringBackend() {
    if (initialized_) {
        io_uring_queue_exit(&ring_);
        initialized_ = false;
    }
}

IoUringBackend::IoUringBackend(IoUringBackend&& other) noexcept
    : initialized_(other.initialized_), sqpoll_active_(other.sqpoll_active_), ring_(other.ring_) {
    other.initialized_ = false;
}

IoUringBackend& IoUringBackend::operator=(IoUringBackend&& other) noexcept {
    if (this != &other) {
        if (initialized_) {
            io_uring_queue_exit(&ring_);
        }
        initialized_ = other.initialized_;
        sqpoll_active_ = other.sqpoll_active_;
        ring_ = other.ring_;
        other.initialized_ = false;
    }
    return *this;
}

bool IoUringBackend::setup(unsigned entries) {
    if (initialized_) {
        io_uring_queue_exit(&ring_);
        initialized_ = false;
    }

    // Attempt SQPOLL first
    unsigned int flags = 0;
#ifdef IORING_SETUP_SQPOLL
    flags |= IORING_SETUP_SQPOLL;
#endif

    int ret = -1;
    if (flags != 0) {
        ret = io_uring_queue_init(entries, &ring_, flags);
        if (ret == 0) {
            sqpoll_active_ = true;
            initialized_ = true;
            return true;
        }
        // Fall back gracefully without SQPOLL
        sqpoll_active_ = false;
    }

    ret = io_uring_queue_init(entries, &ring_, 0);
    if (ret < 0) {
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    return true;
}

bool IoUringBackend::submit_accept(int listen_fd, std::function<void(int)> cb) {
    if (!initialized_) return false;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return false;

    io_uring_prep_accept(sqe, listen_fd, nullptr, nullptr, SOCK_NONBLOCK);
    auto* op = new Operation{
        Operation::Type::Accept,
        [callback = std::move(cb)](ssize_t res) {
            callback(static_cast<int>(res));
        }
    };
    io_uring_sqe_set_data(sqe, op);
    return io_uring_submit(&ring_) >= 0;
}

bool IoUringBackend::submit_read(int fd, void* buf, size_t len, off_t offset, std::function<void(ssize_t)> cb) {
    if (!initialized_) return false;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return false;

    io_uring_prep_read(sqe, fd, buf, static_cast<unsigned>(len), offset);
    auto* op = new Operation{Operation::Type::Read, std::move(cb)};
    io_uring_sqe_set_data(sqe, op);
    return io_uring_submit(&ring_) >= 0;
}

bool IoUringBackend::submit_write(int fd, const void* buf, size_t len, off_t offset, std::function<void(ssize_t)> cb) {
    if (!initialized_) return false;
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (!sqe) return false;

    io_uring_prep_write(sqe, fd, buf, static_cast<unsigned>(len), offset);
    auto* op = new Operation{Operation::Type::Write, std::move(cb)};
    io_uring_sqe_set_data(sqe, op);
    return io_uring_submit(&ring_) >= 0;
}

int IoUringBackend::process_completions() {
    if (!initialized_) return 0;
    struct io_uring_cqe* cqe = nullptr;
    int processed = 0;

    while (io_uring_peek_cqe(&ring_, &cqe) == 0 && cqe != nullptr) {
        auto* op = static_cast<Operation*>(io_uring_cqe_get_data(cqe));
        if (op) {
            if (op->callback) {
                op->callback(cqe->res);
            }
            delete op;
        }
        io_uring_cqe_seen(&ring_, cqe);
        cqe = nullptr;
        processed++;
    }
    return processed;
}

#else

// Stubs when compile-time toggle is OFF
IoUringBackend::IoUringBackend() = default;
IoUringBackend::~IoUringBackend() = default;
IoUringBackend::IoUringBackend(IoUringBackend&&) noexcept = default;
IoUringBackend& IoUringBackend::operator=(IoUringBackend&&) noexcept = default;

bool IoUringBackend::setup(unsigned) { return false; }
bool IoUringBackend::submit_accept(int, std::function<void(int)>) { return false; }
bool IoUringBackend::submit_read(int, void*, size_t, off_t, std::function<void(ssize_t)>) { return false; }
bool IoUringBackend::submit_write(int, const void*, size_t, off_t, std::function<void(ssize_t)>) { return false; }
int IoUringBackend::process_completions() { return 0; }

#endif

} // namespace socketline
