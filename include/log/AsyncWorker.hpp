#pragma once
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <functional>
namespace mylog {

class Buffer {
public:
    static const size_t kDefaultSize = 4096;
    static const size_t kGrowthFactor = 2;  // 增长因子

    Buffer(size_t size = kDefaultSize) : buffer_(size), read_pos_(0), write_pos_(0) {}

    size_t ReadableSize() const {
        return write_pos_ - read_pos_;
    }

    size_t WritableSize() const {
        return buffer_.size() - write_pos_;
    }

    const char* Begin() const {
        return &buffer_[read_pos_];
    }

    void Consume(size_t len) {
        read_pos_ += len;
        if (read_pos_ == write_pos_) {
            read_pos_ = write_pos_ = 0;
        }
    }

    void Append(const char* data, size_t len) {
        if (WritableSize() < len) {
            // 按因子增长，减少频繁分配
            size_t new_size = std::max(buffer_.size() * kGrowthFactor, write_pos_ + len);
            buffer_.resize(new_size);
        }
        memcpy(&buffer_[write_pos_], data, len);
        write_pos_ += len;
    }

private:
    std::vector<char> buffer_;
    size_t read_pos_;
    size_t write_pos_;
};

enum class AsyncType {
    ASYNC_SAFE,
    ASYNC_FAST
};

class AsyncWorker {
public:
    using ptr = std::shared_ptr<AsyncWorker>;
    using FlushCallback = std::function<void(Buffer&)>;

    AsyncWorker(FlushCallback callback, AsyncType type) 
        : callback_(callback), type_(type), running_(true), thread_([this]() { Work(); }) {}

    ~AsyncWorker() {
        running_ = false;
        cond_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    void Push(const char* data, size_t len) {
        std::unique_lock<std::mutex> lock(mtx_);
        buffer_.Append(data, len);
        cond_.notify_one();
    }

private:
    void Work() {
        while (running_) {
            Buffer buffer;
            {
                std::unique_lock<std::mutex> lock(mtx_);
                cond_.wait(lock, [this]() { return !running_ || buffer_.ReadableSize() > 0; });
                if (!running_ && buffer_.ReadableSize() == 0) {
                    break;
                }
                std::swap(buffer, buffer_);
            }
            if (buffer.ReadableSize() > 0) {
                callback_(buffer);
            }
        }
    }

private:
    FlushCallback callback_;
    AsyncType type_;
    std::atomic<bool> running_;
    std::mutex mtx_;
    std::condition_variable cond_;
    Buffer buffer_;
    std::thread thread_;
};

} // namespace mylog