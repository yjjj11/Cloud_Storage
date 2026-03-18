#pragma once

#include <atomic>
#include <cassert>
#include <fmt/format.h>
#include <memory>
#include <mutex>
// 前向声明
class ThreadPool;

extern ThreadPool *tp;

#include "Level.hpp"
#include "AsyncWorker.hpp"
#include "Message.hpp"
#include "LogFlush.hpp"
#include "ThreadPoll.hpp"

namespace mylog {

class AsyncLogger {
public:
    using ptr = std::shared_ptr<AsyncLogger>;

    AsyncLogger(const std::string &logger_name, std::vector<LogFlush::ptr> &flushs, AsyncType type)
        : logger_name_(logger_name),
          flushs_(flushs.begin(), flushs.end()),
          asyncworker(std::make_shared<AsyncWorker>(
              std::bind(&AsyncLogger::RealFlush, this, std::placeholders::_1),
              type)) {}

    virtual ~AsyncLogger() = default;

    std::string Name() { return logger_name_; }

    template<typename... Args>
    void Debug(const std::string &file, size_t line, std::string_view format, Args&&... args) {
        log(LogLevel::value::DEBUG, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Info(const std::string &file, size_t line, std::string_view format, Args&&... args) {
        log(LogLevel::value::INFO, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Warn(const std::string &file, size_t line, std::string_view format, Args&&... args) {
        log(LogLevel::value::WARN, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Error(const std::string &file, size_t line, std::string_view format, Args&&... args) {
        log(LogLevel::value::ERROR, file, line, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void Fatal(const std::string &file, size_t line, std::string_view format, Args&&... args) {
        log(LogLevel::value::FATAL, file, line, format, std::forward<Args>(args)...);
    }

private:
    template<typename... Args>
    void log(LogLevel::value level, const std::string &file, size_t line, std::string_view format, Args&&... args) {
        try {
            std::string payload = fmt::vformat(format, fmt::make_format_args(std::forward<Args>(args)...));
            serialize(level, file, line, payload);
        } catch (const fmt::format_error& e) {
            std::cerr << "Format error: " << e.what() << std::endl;
        }
    }

protected:
    void serialize(LogLevel::value level, const std::string &file, size_t line, const std::string &payload) {
        LogMessage msg(level, file, line, logger_name_, payload);
        std::string data = msg.format();
        Flush(data.c_str(), data.size());
    }

    void Flush(const char *data, size_t len) {
        asyncworker->Push(data, len);
    }

    void RealFlush(Buffer &buffer) {
        if (flushs_.empty())
            return;
        for (auto &e : flushs_) {
            e->Flush(buffer.Begin(), buffer.ReadableSize());
        }
    }

protected:
    std::mutex mtx_;
    std::string logger_name_;
    std::vector<LogFlush::ptr> flushs_;
    AsyncWorker::ptr asyncworker;
};

class LoggerBuilder {
public:
    using ptr = std::shared_ptr<LoggerBuilder>;

    void BuildLoggerName(const std::string &name) { logger_name_ = name; }
    void BuildLopperType(AsyncType type) { async_type_ = type; }

    template <typename FlushType, typename... Args>
    void BuildLoggerFlush(Args &&...args) {
        flushs_.emplace_back(LogFlushFactory::CreateLog<FlushType>(std::forward<Args>(args)...));
    }

    AsyncLogger::ptr Build() {
        assert(!logger_name_.empty());

        if (flushs_.empty()) {
            flushs_.emplace_back(std::make_shared<StdoutFlush>());
        }

        return std::make_shared<AsyncLogger>(logger_name_, flushs_, async_type_);
    }

protected:
    std::string logger_name_ = "async_logger";
    std::vector<LogFlush::ptr> flushs_;
    AsyncType async_type_ = AsyncType::ASYNC_SAFE;
};

} // namespace mylog

// 便捷的日志宏，自动获取文件名和行号
#define LOG_DEBUG(logger, ...) (logger)->Debug(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(logger, ...) (logger)->Info(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(logger, ...) (logger)->Warn(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(logger, ...) (logger)->Error(__FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(logger, ...) (logger)->Fatal(__FILE__, __LINE__, __VA_ARGS__)
