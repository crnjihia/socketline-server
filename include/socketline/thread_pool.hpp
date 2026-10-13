#pragma once

#include <functional>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include <cstdint>
#include <condition_variable>
#include <mutex>

namespace socketline {

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(size_t num_threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void enqueue(Task task);
    size_t size() const { return workers_.size(); }
    void stop();

private:
    class ChaseLevDeque {
    public:
        explicit ChaseLevDeque(size_t capacity = 4096);
        ~ChaseLevDeque();

        ChaseLevDeque(const ChaseLevDeque&) = delete;
        ChaseLevDeque& operator=(const ChaseLevDeque&) = delete;

        bool push(Task task);
        bool pop(Task& out_task);
        bool steal(Task& out_task);

    private:
        std::atomic<int64_t> top_{0};
        std::atomic<int64_t> bottom_{0};
        std::unique_ptr<Task[]> buffer_;
        size_t capacity_;
    };

    struct Worker {
        size_t id;
        std::thread thread;
        ChaseLevDeque deque;
    };

    void worker_loop(size_t worker_id);

    std::vector<std::unique_ptr<Worker>> workers_;
    std::atomic<bool> stop_{false};
    std::atomic<size_t> next_worker_{0};

    std::mutex notify_mutex_;
    std::condition_variable notify_cv_;
};

} // namespace socketline
