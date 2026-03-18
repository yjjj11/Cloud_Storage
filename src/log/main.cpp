#include <iostream>
#include "log/ThreadPoll.hpp"
#include "log/AsyncLogger.hpp"
#include "log/LogFlush.hpp"
#include "log/Level.hpp"

// 全局线程池
ThreadPool *tp;

int main() {
    // 初始化线程池
    tp = new ThreadPool(4);

    
    // 创建日志器构建器
    mylog::LoggerBuilder builder;
    builder.BuildLoggerName("TestLogger");
    
    // 添加文件输出
    builder.BuildLoggerFlush<mylog::FileFlush>("logs/test.log");
    
    // 添加标准输出
    builder.BuildLoggerFlush<mylog::StdoutFlush>();
    
    // 构建日志器
    auto logger = builder.Build();
    
    // 测试不同级别的日志（使用便捷的宏，自动获取文件名和行号）
    LOG_DEBUG(logger, "Debug message: {}", "Hello, Debug!");
    LOG_INFO(logger, "Info message: {}", 42);
    LOG_WARN(logger, "Warn message: {}", 3.14);
    LOG_ERROR(logger, "Error message: {}", true);
    LOG_FATAL(logger, "Fatal message: {}", "Critical error");
    
    std::cout << "Log test completed!" << std::endl;
    
    // 清理线程池
    delete tp;
    
    return 0;
}
