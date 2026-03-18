#pragma once

#include <cassert>
#include <fstream>
#include <memory>
#include <unistd.h>

#include "Util.hpp"

namespace mylog {

class LogFlush {
public:
    using ptr = std::shared_ptr<LogFlush>;
    virtual ~LogFlush() = default;
    virtual void Flush(const char *data, size_t len) = 0;
};

class StdoutFlush : public LogFlush {
public:
    using ptr = std::shared_ptr<StdoutFlush>;
    void Flush(const char *data, size_t len) override {
        std::cout.write(data, len);
    }
};

class FileFlush : public LogFlush {
public:
    using ptr = std::shared_ptr<FileFlush>;
    FileFlush(const std::string &filename) : filename_(filename) {
        Util::File::CreateDirectory(Util::File::Path(filename));
        fs_ = fopen(filename.c_str(), "ab");
        if (!fs_) {
            std::cerr << "[" << __FILE__ << ":" << __LINE__ << "] open log file failed: ";
            perror(nullptr);
        }
    }

    ~FileFlush() override {
        if (fs_) {
            fclose(fs_);
            fs_ = nullptr;
        }
    }

    void Flush(const char *data, size_t len) override {
        fwrite(data, 1, len, fs_);
        if (ferror(fs_)) {
            std::cerr << "[" << __FILE__ << ":" << __LINE__ << "] write log file failed: ";
            perror(nullptr);
        }
        auto g_conf_data = Util::JsonData::GetJsonData();
        if (g_conf_data->flush_log == 1) {
            if (fflush(fs_) == EOF) {
                std::cerr << "[" << __FILE__ << ":" << __LINE__ << "] fflush file failed: ";
                perror(nullptr);
            }
        } else if (g_conf_data->flush_log == 2) {
            fflush(fs_);
            fsync(fileno(fs_));
        }
    }

private:
    std::string filename_;
    FILE* fs_ = nullptr;
};

class RollFileFlush : public LogFlush {
public:
    using ptr = std::shared_ptr<RollFileFlush>;
    RollFileFlush(const std::string &filename, size_t max_size)
        : max_size_(max_size), basename_(filename) {
        Util::File::CreateDirectory(Util::File::Path(filename));
    }

    ~RollFileFlush() override {
        if (fs_) {
            fclose(fs_);
            fs_ = nullptr;
        }
    }

    void Flush(const char *data, size_t len) override {
        InitLogFile();
        fwrite(data, 1, len, fs_);
        if (ferror(fs_)) {
            std::cerr << "[" << __FILE__ << ":" << __LINE__ << "] write log file failed: ";
            perror(nullptr);
        }
        cur_size_ += len;
        auto g_conf_data = Util::JsonData::GetJsonData();
        if (g_conf_data->flush_log == 1) {
            if (fflush(fs_)) {
                std::cerr << "[" << __FILE__ << ":" << __LINE__ << "] fflush file failed: ";
                perror(nullptr);
            }
        } else if (g_conf_data->flush_log == 2) {
            fflush(fs_);
            fsync(fileno(fs_));
        }
    }

private:
    void InitLogFile() {
        if (!fs_ || cur_size_ >= max_size_) {
            if (fs_) {
                fclose(fs_);
                fs_ = nullptr;
            }
            std::string filename = CreateFilename();
            fs_ = fopen(filename.c_str(), "ab");
            if (!fs_) {
                std::cerr << "[" << __FILE__ << ":" << __LINE__ << "] open file failed: ";
                perror(nullptr);
            }
            cur_size_ = 0;
        }
    }

    std::string CreateFilename() {
        time_t time_ = Util::Date::Now();
        struct tm t;
        localtime_r(&time_, &t);
        std::stringstream ss;
        ss << basename_ << std::setw(4) << std::setfill('0') << t.tm_year + 1900
           << std::setw(2) << std::setfill('0') << t.tm_mon + 1
           << std::setw(2) << std::setfill('0') << t.tm_mday
           << std::setw(2) << std::setfill('0') << t.tm_hour
           << std::setw(2) << std::setfill('0') << t.tm_min
           << std::setw(2) << std::setfill('0') << t.tm_sec
           << "-" << cnt_++ << ".log";
        return ss.str();
    }

private:
    size_t cnt_ = 1;
    size_t cur_size_ = 0;
    size_t max_size_;
    std::string basename_;
    FILE* fs_ = nullptr;
};

class LogFlushFactory {
public:
    using ptr = std::shared_ptr<LogFlushFactory>;

    template <typename FlushType, typename... Args>
    static std::shared_ptr<LogFlush> CreateLog(Args &&...args) {
        return std::make_shared<FlushType>(std::forward<Args>(args)...);
    }
};

} // namespace mylog