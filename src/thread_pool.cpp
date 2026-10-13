#include "socketline/thread_pool.hpp"
#include <algorithm>
#include <chrono>

namespace socketline {

// -------------------------------------------------------------
// Chase-Lev Deque Implementation
// -------------------------------------------------------------

ThreadPool::ChaseLevDeque::ChaseLevDeque(size_t capacity)
    : capacity_(capacity) {
    buffer_ = std::make_unique<Task[]>(capacity_);
}

ThreadPool::ChaseLevDeque::~ChaseLevDeque() = default;

bool ThreadPool::ChaseLevDeque::push(Task task) {
    int64_t b = bottom_.load(std::memory_order_relaxed);
    int64_t t = top_.load(std::memory_order_acquire);

    if (b - t >= static_cast<int64_t>(capacity_ - 1)) {
        // Buffer is full for this fixed ring
        return false;
    }

    buffer_[static_cast<size_t>(b % capacity_)] = std::move(task);
    bottom_.store(b + 1, std::memory_order_release);
    return true;
}

bool ThreadPool::ChaseLevDeque::pop(Task& out_task) {
    int64_t b = bottom_.load(std::memory_order_relaxed) - 1;
    bottom_.store(b, std::memory_order_seq_cst);

    int64_t t = top_.load(std::memory_order_seq_cst);
    if (t <= b) {
        // Non-empty queue
        out_task = std::move(buffer_[static_cast<size_t>(b % capacity_)]);
        if (t == b) {
            // Last element: race against stealers
            if (!top_.compare_exchange_strong(t, t + 1,
                                              std::memory_order_seq_cst,
                                              std::memory_order_relaxed)) {
                // Lost race to stealer
                out_task = nullptr;
            }
            bottom_.store(b + 1, std::memory_order_relaxed);
            return out_task != nullptr;
        }
        return true;
    }

    // Queue was empty
    bottom_.store(b + 1, std::memory_order_relaxed);
    return false;
}

bool ThreadPool::ChaseLevDeque::steal(Task& out_task) {
    int64_t t = top_.load(std::memory_order_seq_cst);
    int64_t b = bottom_.load(std::memory_order_seq_cst);

    if (t < b) {
        // Non-empty: take task at top
        out_task = buffer_[static_cast<size_t>(t % capacity_)];
        if (!top_.compare_exchange_strong(t, t + 1,
                                          std::memory_order_seq_cst,
                                          std::memory_order_relaxed)) {
            // Lost race
            out_task = nullptr;
            return false;
        }
        return true;
    }
    return false;
}

// -------------------------------------------------------------
// ThreadPool Implementation
// -------------------------------------------------------------

ThreadPool::ThreadPool(size_t num_threads) {
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
    }
    if (num_threads == 0) {
        num_threads = 2;
    }

    workers_.reserve(num_threads);
    for (size_t i = 0; i < num_threads; ++i) {
        auto w = std::make_unique<Worker>();
        w->id = i;
        workers_.push_back(std::move(w));
    }

    for (size_t i = 0; i < num_threads; ++i) {
        workers_[i]->thread = std::thread(&ThreadPool::worker_loop, this, i);
    }
}

ThreadPool::~ThreadPool() {
    stop();
}

void ThreadPool::stop() {
    bool expected = false;
    if (stop_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        notify_cv_.notify_all();
        for (auto& w : workers_) {
            if (w->thread.joinable()) {
                w->thread.join();
            }
        }
    }
}

void ThreadPool::enqueue(Task task) {
    if (stop_.load(std::memory_order_relaxed) || !task) return;

    size_t target = next_worker_.fetch_add(1, std::memory_order_relaxed) % workers_.size();
    if (!workers_[target]->deque.push(std::move(task))) {
        // If target queue is full, try next
        for (size_t i = 1; i < workers_.size(); ++i) {
            size_t idx = (target + i) % workers_.size();
            if (workers_[idx]->deque.push(task)) {
                break;
            }
        }
    }
    notify_cv_.notify_one();
}

void ThreadPool::worker_loop(size_t worker_id) {
    Worker* self = workers_[worker_id].get();
    Task task;

    while (!stop_.load(std::memory_order_acquire)) {
        // 1. Pop from local queue
        if (self->deque.pop(task)) {
            if (task) {
                task();
                task = nullptr;
            }
            continue;
        }

        // 2. Steal from others
        bool found = false;
        size_t n = workers_.size();
        for (size_t i = 1; i < n; ++i) {
            size_t victim = (worker_id + i) % n;
            if (workers_[victim]->deque.steal(task)) {
                found = true;
                break;
            }
        }

        if (found && task) {
            task();
            task = nullptr;
            continue;
        }

        // 3. Back off if all queues appear idle
        std::unique_lock<std::mutex> lock(notify_mutex_);
        notify_cv_.wait_for(lock, std::chrono::microseconds(500), [&]() {
            return stop_.load(std::memory_order_relaxed);
        });
    }

    // Drain remaining local tasks before exit
    while (self->deque.pop(task)) {
        if (task) {
            task();
            task = nullptr;
        }
    }
}

} // namespace socketline
