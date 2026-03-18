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
#include <mrpc/connection.hpp>
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
    mkdir(dir.c_str(), 0755);
}

// 生成日志文件名
std::string generate_log_filename() {
    time_t now = time(nullptr);
    struct tm *tm_info = localtime(&now);
    char filename[128];
    strftime(filename, sizeof(filename), "%Y%m%d_%H%M%S.log", tm_info);
    return std::string(filename);
}

// 存储模块路由（子类）
class StorageRouter : public Router {
public:
    StorageRouter(HttpService &service, std::shared_ptr<mrpc::connection> storage_conn)
        : Router(service), storage_conn_(storage_conn) {}
    ~StorageRouter() = default;

    void RegisterRoute() override {
        // 健康检查接口
        service_.GET("/health", [](HttpRequest* req, HttpResponse* res) -> int {
            res->SetBody("Server is running");
            return HTTP_STATUS_OK;
        });
        
        // 根路径HTML响应
        service_.GET("/", [](HttpRequest* req, HttpResponse* res) -> int {
            // 从文件读取HTML内容
            std::string html;
            std::ifstream html_file("include/resource/index.html");
            if (html_file.is_open()) {
                std::string line;
                while (std::getline(html_file, line)) {
                    html += line + "\n";
                }
                html_file.close();
            } else {
                html = "<h1>Log Backup Server</h1><p>HTML file not found</p>";
            }
            res->content_type = TEXT_HTML;
            res->SetBody(html);
            return HTTP_STATUS_OK;
        });
        
        // 云存储接口
        // 1. 文件上传
        service_.POST("/api/storage/upload", [this](HttpRequest* req, HttpResponse* res) -> int {
            // 获取文件名
            std::string filename = req->GetParam("filename", "");
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 获取文件内容
            std::string file_content = req->body;
            if (file_content.empty()) {
                res->SetBody("File content is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 通过RPC调用存储节点的上传服务
            if (storage_conn_) {
                try {
                    auto result = storage_conn_->call<std::string>("upload_file", filename, file_content);
                    std::string res_str = result.value();
                    res->SetBody(res_str);
                    return HTTP_STATUS_OK;
                } catch (const std::exception& e) {
                    res->SetBody("RPC error: " + std::string(e.what()));
                    return HTTP_STATUS_INTERNAL_SERVER_ERROR;
                }
            } else {
                res->SetBody("No connection to storage node");
                return HTTP_STATUS_SERVICE_UNAVAILABLE;
            }
        });
        
        // 2. 文件下载
        service_.GET("/api/storage/download/{filename}", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = req->GetParam("filename", "");
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 通过RPC调用存储节点的下载服务
            if (storage_conn_) {
                try {
                    auto result = storage_conn_->call<std::string>("download_file", filename);
                    std::string file_content = result.value();
                    if (file_content.empty()) {
                        res->SetBody("File not found");
                        return HTTP_STATUS_NOT_FOUND;
                    }
                    // 设置响应头
                    res->content_type = APPLICATION_OCTET_STREAM;
                    res->SetHeader("Content-Disposition", "attachment; filename=" + filename);
                    res->SetBody(file_content);
                    return HTTP_STATUS_OK;
                } catch (const std::exception& e) {
                    res->SetBody("RPC error: " + std::string(e.what()));
                    return HTTP_STATUS_INTERNAL_SERVER_ERROR;
                }
            } else {
                res->SetBody("No connection to storage node");
                return HTTP_STATUS_SERVICE_UNAVAILABLE;
            }
        });
        
        // 3. 列举文件
        service_.GET("/api/storage/list", [this](HttpRequest* req, HttpResponse* res) -> int {
            // 通过RPC调用存储节点的列举服务
            if (storage_conn_) {
                try {
                    auto result = storage_conn_->call<std::string>("list_files", 0);
                    std::string file_list = result.value();
                    res->SetBody(file_list);
                    return HTTP_STATUS_OK;
                } catch (const std::exception& e) {
                    res->SetBody("RPC error: " + std::string(e.what()));
                    return HTTP_STATUS_INTERNAL_SERVER_ERROR;
                }
            } else {
                res->SetBody("No connection to storage node");
                return HTTP_STATUS_SERVICE_UNAVAILABLE;
            }
        });
        
        // 4. 删除文件
        service_.Delete("/api/storage/delete/{filename}", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = req->GetParam("filename", "");
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 通过RPC调用存储节点的删除服务
            if (storage_conn_) {
                try {
                    auto result = storage_conn_->call<std::string>("delete_file", filename);
                    res->SetBody(result.value());
                    return HTTP_STATUS_OK;
                } catch (const std::exception& e) {
                    res->SetBody("RPC error: " + std::string(e.what()));
                    return HTTP_STATUS_INTERNAL_SERVER_ERROR;
                }
            } else {
                res->SetBody("No connection to storage node");
                return HTTP_STATUS_SERVICE_UNAVAILABLE;
            }
        });
    }
private:
    std::shared_ptr<mrpc::connection> storage_conn_;
};