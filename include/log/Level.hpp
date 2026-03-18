#pragma once
#include <string>
namespace mylog {

class LogLevel {
public:
    enum class value {
        DEBUG = 0,
        INFO,
        WARN,
        ERROR,
        FATAL
    };

    static std::string ToString(value level) {
        switch (level) {
        case value::DEBUG:
            return "DEBUG";
        case value::INFO:
            return "INFO";
        case value::WARN:
            return "WARN";
        case value::ERROR:
            return "ERROR";
        case value::FATAL:
            return "FATAL";
        default:
            return "UNKNOWN";
        }
    }
};

} // namespace mylog
