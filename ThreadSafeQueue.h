#pragma once

#include <queue>
#include <mutex>

namespace WebS {

template<typename T>
class ThreadSafeQueue {
public:
    void push(T item) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(item));
    }

    bool tryPop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) {
            return false;
        }
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }

    // Swap contents with another queue (useful for batch processing)
    void swap(std::queue<T>& other) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::swap(queue_, other);
    }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
};

} // namespace WebS
