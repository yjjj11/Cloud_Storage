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
    StorageRouter(HttpService &service) : Router(service) {}
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
            std::ifstream html_file("../include/resource/index.html");
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
        service_.POST("/api/storage/upload", [](HttpRequest* req, HttpResponse* res) -> int {
            // 确保存储目录存在
            ensure_directory(STORAGE_DIR);
            
            // 获取文件名
            std::string filename = req->GetParam("filename", "");
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 获取文件内容
            std::string file_content;
            
            // 检查是否是multipart/form-data格式
            std::string content_type = req->GetHeader("Content-Type", "");
            if (content_type.find("multipart/form-data") != std::string::npos) {
                // 解析multipart/form-data
                // 这里简化处理，直接使用req->body
                // 实际生产环境中应该使用更健壮的解析方法
                file_content = req->body;
            } else {
                // 直接使用body
                file_content = req->body;
            }
            
            if (file_content.empty()) {
                res->SetBody("File content is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 存储文件
            { 
                std::lock_guard<std::mutex> lock(log_mutex);
                std::string filepath = STORAGE_DIR + "/" + filename;
                std::ofstream file(filepath, std::ios::binary);
                if (file.is_open()) {
                    file << file_content;
                    file.close();
                    res->SetBody("File uploaded successfully: " + filename);
                    return HTTP_STATUS_OK;
                } else {
                    res->SetBody("Failed to upload file");
                    return HTTP_STATUS_INTERNAL_SERVER_ERROR;
                }
            }
        });
        
        // 2. 文件下载
        service_.GET("/api/storage/download/{filename}", [](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = req->GetParam("filename", "");
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            // 读取文件
            std::string filepath = STORAGE_DIR + "/" + filename;
            std::ifstream file(filepath, std::ios::binary);
            if (!file.is_open()) {
                res->SetBody("File not found");
                return HTTP_STATUS_NOT_FOUND;
            }
            
            // 读取文件内容
            std::string file_content((std::istreambuf_iterator<char>(file)), 
                                     std::istreambuf_iterator<char>());
            file.close();
            
            // 设置响应头
            res->content_type = APPLICATION_OCTET_STREAM;
            res->SetHeader("Content-Disposition", "attachment; filename=" + filename);
            res->SetBody(file_content);
            return HTTP_STATUS_OK;
        });
        
        // 3. 列举文件
        service_.GET("/api/storage/list", [](HttpRequest* req, HttpResponse* res) -> int {
            ensure_directory(STORAGE_DIR);
            
            std::string file_list = "Files:\n";
            DIR* dir = opendir(STORAGE_DIR.c_str());
            if (dir) {
                struct dirent* entry;
                while ((entry = readdir(dir)) != nullptr) {
                    if (entry->d_type == DT_REG) {
                        file_list += std::string("- ") + entry->d_name + "\n";
                    }
                }
                closedir(dir);
            }
            
            res->SetBody(file_list);
            return HTTP_STATUS_OK;
        });
        
        // 4. 删除文件
        service_.Delete("/api/storage/delete/{filename}", [](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = req->GetParam("filename", "");
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            std::string filepath = STORAGE_DIR + "/" + filename;
            if (remove(filepath.c_str()) == 0) {
                res->SetBody("File deleted successfully: " + filename);
                return HTTP_STATUS_OK;
            } else {
                res->SetBody("Failed to delete file");
                return HTTP_STATUS_INTERNAL_SERVER_ERROR;
            }
        });
    }
};