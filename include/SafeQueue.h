#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <cstddef>
#include <utility>

// Thread-safe multi-producer/multi-consumer blocking queue.
// maxSize == 0 means unbounded; otherwise push() blocks when the queue is full.
template<typename T>
class SafeQueue {
public:
    explicit SafeQueue(std::size_t maxSize = 0) : maxSize_(maxSize) {}

    SafeQueue(const SafeQueue&) = delete;
    SafeQueue& operator=(const SafeQueue&) = delete;

    bool push(T value) {
        std::unique_lock<std::mutex> lock(m_);
        notFull_.wait(lock, [&] {
            return finished_ || maxSize_ == 0 || q_.size() < maxSize_;
        });
        if (finished_) return false;

        q_.push(std::move(value));
        notEmpty_.notify_one();
        return true;
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(m_);
        notEmpty_.wait(lock, [&] { return !q_.empty() || finished_; });
        if (q_.empty()) return false;

        value = std::move(q_.front());
        q_.pop();
        notFull_.notify_one();
        return true;
    }

    void setFinished() {
        std::lock_guard<std::mutex> lock(m_);
        finished_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(m_);
        return q_.size();
    }

    bool isFinished() const {
        std::lock_guard<std::mutex> lock(m_);
        return finished_;
    }

    std::size_t maxSize() const { return maxSize_; }

private:
    std::queue<T> q_;
    mutable std::mutex m_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    bool finished_ = false;
    std::size_t maxSize_ = 0;
};
