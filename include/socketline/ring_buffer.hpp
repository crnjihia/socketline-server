#pragma once

#include <vector>
#include <mutex>
#include <cstddef>
#include <cstdint>

namespace socketline {

/**
 * Thread-safe fixed-capacity circular ring buffer with overwrite on overflow.
 * Designed for retaining the last N events (e.g. 1000) for replay in FIFO order.
 */
template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity = 1000)
        : capacity_(capacity > 0 ? capacity : 1),
          buffer_(capacity > 0 ? capacity : 1) {}

    RingBuffer(const RingBuffer& other) {
        std::lock_guard<std::mutex> lock(other.mutex_);
        capacity_ = other.capacity_;
        buffer_ = other.buffer_;
        head_ = other.head_;
        tail_ = other.tail_;
        size_ = other.size_;
        total_pushed_ = other.total_pushed_;
    }

    RingBuffer& operator=(const RingBuffer& other) {
        if (this != &other) {
            std::scoped_lock lock(mutex_, other.mutex_);
            capacity_ = other.capacity_;
            buffer_ = other.buffer_;
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            total_pushed_ = other.total_pushed_;
        }
        return *this;
    }

    RingBuffer(RingBuffer&& other) noexcept {
        std::lock_guard<std::mutex> lock(other.mutex_);
        capacity_ = other.capacity_;
        buffer_ = std::move(other.buffer_);
        head_ = other.head_;
        tail_ = other.tail_;
        size_ = other.size_;
        total_pushed_ = other.total_pushed_;
        other.size_ = 0;
        other.head_ = 0;
        other.tail_ = 0;
        other.total_pushed_ = 0;
    }

    RingBuffer& operator=(RingBuffer&& other) noexcept {
        if (this != &other) {
            std::scoped_lock lock(mutex_, other.mutex_);
            capacity_ = other.capacity_;
            buffer_ = std::move(other.buffer_);
            head_ = other.head_;
            tail_ = other.tail_;
            size_ = other.size_;
            total_pushed_ = other.total_pushed_;
            other.size_ = 0;
            other.head_ = 0;
            other.tail_ = 0;
            other.total_pushed_ = 0;
        }
        return *this;
    }

    /**
     * Push item into the buffer. Overwrites oldest item if capacity is reached.
     * Returns true always.
     */
    bool push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % capacity_;

        if (size_ < capacity_) {
            size_++;
        } else {
            // Overwriting oldest entry: advance head
            head_ = (head_ + 1) % capacity_;
        }
        total_pushed_++;
        return true;
    }

    /**
     * Pop oldest item (FIFO).
     */
    bool pop(T& out_item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (size_ == 0) return false;

        out_item = std::move(buffer_[head_]);
        head_ = (head_ + 1) % capacity_;
        size_--;
        return true;
    }

    /**
     * Returns snapshot of all current items from oldest to newest (FIFO).
     */
    std::vector<T> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<T> result;
        result.reserve(size_);
        for (size_t i = 0; i < size_; ++i) {
            size_t idx = (head_ + i) % capacity_;
            result.push_back(buffer_[idx]);
        }
        return result;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

    size_t capacity() const {
        return capacity_;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == 0;
    }

    bool full() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_ == capacity_;
    }

    uint64_t total_pushed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return total_pushed_;
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        head_ = 0;
        tail_ = 0;
        size_ = 0;
    }

private:
    size_t capacity_{1000};
    std::vector<T> buffer_;
    size_t head_{0};
    size_t tail_{0};
    size_t size_{0};
    uint64_t total_pushed_{0};
    mutable std::mutex mutex_;
};

} // namespace socketline
