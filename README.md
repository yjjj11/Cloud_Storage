# 高性能异步日志系统

## 项目简介

这是一个基于现代C++20的高性能异步日志系统，支持多级别日志、异步处理、多种输出方式和远程备份功能。

## 目录结构

```
new_system/
├── bin/            # 可执行文件目录
├── build/          # 编译目录
├── config/         # 配置文件目录
├── include/        # 头文件目录
│   └── log/        # 日志系统头文件
├── src/            # 源代码目录
│   └── log/        # 日志系统源代码
└── CMakeLists.txt  # CMake配置文件
```

## 核心功能

1. **异步处理**：通过后台线程处理日志写入，避免阻塞主线程
2. **多级别日志**：支持Debug、Info、Warn、Error、Fatal五个级别
3. **多种输出方式**：
   - 标准输出
   - 文件输出
   - 滚动文件输出
4. **远程备份**：对Error和Fatal级别的日志进行远程备份
5. **可配置性**：通过config.conf文件调整系统参数

## 技术特点

- 使用C++20的std::format进行日志格式化，类型安全且内存管理自动化
- 使用nlohmann/json进行配置文件解析，API简洁且功能强大
- 采用现代C++特性：智能指针、lambda表达式、std::bind等
- 模块化设计，易于扩展和维护

## 配置文件

配置文件位于`config/config.conf`，包含以下参数：

```json
{
    "buffer_size": 4096,        # 缓冲区基础容量
    "threshold": 8192,          # 倍数扩容阈值
    "linear_growth": 4096,      # 线性增长容量
    "flush_log": 1,             # 日志同步策略（0：默认，1：fflush，2：fsync）
    "backup_addr": "127.0.0.1", # 备份服务器地址
    "backup_port": 8888,        # 备份服务器端口
    "thread_count": 4           # 线程数量
}
```

## 使用方法

### 1. 编译项目

```bash
cd new_system
mkdir -p build && cd build
cmake ..
make
```

或直接使用g++编译：

```bash
cd new_system
g++ -std=c++20 -Iinclude src/log/main.cpp -o bin/logger -lpthread
```

### 2. 运行示例

```bash
cd new_system
./bin/logger
```

### 3. 集成到项目中

```cpp
#include "log/AsyncLogger.hpp"
#include "log/LogFlush.hpp"

// 全局线程池
ThreadPool *tp;

int main() {
    // 初始化线程池
    tp = new ThreadPool(4);

    // 创建日志器构建器
    mylog::LoggerBuilder builder;
    builder.BuildLoggerName("MyApp");
    
    // 添加文件输出
    builder.BuildLoggerFlush<mylog::FileFlush>("logs/app.log");
    
    // 添加标准输出
    builder.BuildLoggerFlush<mylog::StdoutFlush>();
    
    // 构建日志器
    auto logger = builder.Build();
    
    // 记录日志
    logger->Info(__FILE__, __LINE__, "Application started");
    logger->Error(__FILE__, __LINE__, "Failed to connect: {}", "connection refused");
    
    // 清理线程池
    delete tp;
    
    return 0;
}
```

## 日志格式

日志格式示例：

```
[2026-03-17 12:34:56] [INFO] [TestLogger] [main.cpp:25] [140123456789012] Info message: 42
```

格式说明：
- 时间戳：`[2026-03-17 12:34:56]`
- 日志级别：`[INFO]`
- 日志器名称：`[TestLogger]`
- 文件和行号：`[main.cpp:25]`
- 线程ID：`[140123456789012]`
- 日志内容：`Info message: 42`

## 注意事项

1. 确保`config/config.conf`文件存在且格式正确
2. 远程备份功能需要目标服务器监听在配置的端口上
3. 编译时需要C++20或更高标准
4. 需要链接pthread库
