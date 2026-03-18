#include <iostream>
// 只需包含核心头文件，无需拆分
#include <hv/HttpServer.h>
#include <fstream>
#include <ctime>
#include <string>
#include <mutex>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>

// 路由基类（简化头文件依赖）
class Router {
public:
    // 兼容新旧版 libhv：去掉 hv:: 前缀（旧版无命名空间）
    Router(HttpService &service) : service_(service) {}
    virtual ~Router() = default;
    virtual void RegisterRoute() = 0;

protected:
    HttpService &service_;
};

// 日志存储目录
const std::string LOG_DIR = "logs/backup";
// 云存储目录
const std::string STORAGE_DIR = "storage";

// 互斥锁，用于线程安全的文件操作
std::mutex log_mutex;

// 确保目录存在
void ensure_directory(const std::string &dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        mkdir(dir.c_str(), 0755);
    }
}

// 生成日志文件名
std::string generate_log_filename() {
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char filename[128];
    strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S.log", tm_info);
    return std::string(filename);
}