#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>

template<typename T>
class SafeQueue {
public:
    void push(T value) {
        std::lock_guard<std::mutex> lock(m_);
        q_.push(std::move(value));
        cv_.notify_one();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&] { return !q_.empty() || finished_; });
        if (q_.empty()) return false;
        value = std::move(q_.front());
        q_.pop();
        return true;
    }

    void setFinished() {
        std::lock_guard<std::mutex> lock(m_);
        finished_ = true;
        cv_.notify_all();
    }

    // 查询队列当前元素个数（非阻塞，瞬时值）
    size_t size() const {
        std::lock_guard<std::mutex> lock(m_);
        return q_.size();
    }

    // 查询队列是否已被标记为结束
    bool isFinished() const {
        std::lock_guard<std::mutex> lock(m_);
        return finished_;
    }

private:
    std::queue<T> q_;
    mutable std::mutex m_;
    std::condition_variable cv_;
    bool finished_ = false;
};
