#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <memory>
#include <vector>
#include <mrpc/client.hpp>
#include <iostream>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <string>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <sstream>
#include "route.hpp"
#include "hash.hpp"
#include "../../third_party/raftKV/include/kv_store.hpp"
using namespace mrpc;

// URL解码函数，解决中文文件名乱码问题
std::string urlDecode(const std::string& encoded) {
    std::string result;
    for (size_t i = 0; i < encoded.length(); ++i) {
        if (encoded[i] == '%' && i + 2 < encoded.length()) {
            std::string hexStr = encoded.substr(i + 1, 2);
            char ch = static_cast<char>(std::strtol(hexStr.c_str(), nullptr, 16));
            result += ch;
            i += 2;
        } else if (encoded[i] == '+') {
            result += ' ';
        } else {
            result += encoded[i];
        }
    }
    return result;
}


class StorageRouter : public Router {
public:
    StorageRouter(HttpService &service, ConsistentHash& consistentHash)
        : Router(service), consistentHash_(consistentHash) {
    }
    ~StorageRouter() {
        updateNodeThread_.join();
    }
private:
    void updateNodePort() {
        while (true) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
        }
    }
    
public:
    void RegisterRoute() override {
        // 健康检查接口
        service_.GET("/health", [](HttpRequest* req, HttpResponse* res) -> int {
            res->SetBody("Server is running");
            return HTTP_STATUS_OK;
        });
        
        // 根路径HTML响应
        service_.GET("/", [this](HttpRequest* req, HttpResponse* res) -> int {
            // 使用缓存的HTML内容，支持热更新
            std::string html = getCachedHtml();
            res->content_type = TEXT_HTML;
            res->SetBody(html);
            return HTTP_STATUS_OK;
        });
        
        // 1. 文件上传
        service_.POST("/api/storage/upload", [this](HttpRequest* req, HttpResponse* res) -> int {
            // 获取文件名并进行URL解码，解决中文文件名乱码问题
            std::string filename = urlDecode(req->GetParam("filename", ""));
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            std::cout << "Received filename (decoded): " << filename << std::endl;
            
            // 获取文件内容
            std::string file_content = req->body;
            
            // 生成唯一的fileId
            std::string fileId = generateFileId(filename);
                
            // 存储filename到fileId的映射asd
            
            filename_to_fileId_[filename] = fileId;
                
            // 使用一致性哈希选择存储节点
            NodeInfo node = consistentHash_.getResponsibleNode(fileId);
            std::cout << "Selected storage node: " << node.id << " (port: " << node.port << ")" << std::endl;
            
            // 直接使用NodeInfo中的conn
            auto storage_conn = node.conn;
            
            // 准备元数据
            CloudStorageMetadata metadata;
            metadata.fileId = fileId;
            metadata.filename = filename;
            metadata.size = file_content.size();
            metadata.contentType = "application/octet-stream";
            metadata.updateTime = metadata.createTime;
            metadata.storageClass = "STANDARD";
            metadata.bucketName = std::to_string(node.port);
            
            // 通过RPC调用存储节点的上传服务
            try {
                auto result = storage_conn->call<std::string>("upload_file", metadata, file_content);
                std::string res_str = result.value();
                res->SetBody(res_str);
                return HTTP_STATUS_OK;
            } catch (const std::exception& e) {
                res->SetBody("RPC error: " + std::string(e.what()));
                return HTTP_STATUS_INTERNAL_SERVER_ERROR;
            }
        });
        
        // // 2. 文件下载
        service_.GET("/api/storage/download/{filename}", [this](HttpRequest* req, HttpResponse* res) -> int {
            // 获取文件名并进行URL解码，解决中文文件名乱码问题
            std::string filename = urlDecode(req->GetParam("filename", ""));
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            
            std::cout << "Download request for filename (decoded): " << filename << std::endl;
            // 从RaftKV中获取元数据
            auto filekey=filename_to_fileId_[filename];

            // 使用一致性哈希选择存储节点
            NodeInfo node = consistentHash_.getResponsibleNode(filekey);
            std::cout << "Selected storage node for download: " << node.id << " (port: " << node.port << ")" << std::endl;
            
            // 直接使用NodeInfo中的conn
            auto storage_conn = node.conn;
            
            // 通过RPC调用存储节点的下载服务
            try {
                auto result = storage_conn->call<std::string>("download_file", filename);
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
        });
        
        // 3. 列举文件
        service_.GET("/api/storage/list", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string combined_list = "Files:\n";
            
            for (const auto& pair : consistentHash_.port_to_node_id_) {
                const NodeInfo& node = pair.second;
                try {
                    auto result = node.conn->call<std::string>("list_files", 0);
                    std::string file_list = result.value();
                    combined_list += file_list + "\n";
                } catch (const std::exception& e) {
                    std::cout << "Error listing files from node " << node.id << ": " << e.what() << std::endl;
                }
            }

            
            res->SetBody(combined_list);
            return HTTP_STATUS_OK;
        });
        
        // 4. 删除文件
        service_.Delete("/api/storage/delete/{filename}", [this](HttpRequest* req, HttpResponse* res) -> int {
            // 获取文件名并进行URL解码，解决中文文件名乱码问题
            std::string filename = urlDecode(req->GetParam("filename", ""));
            
            std::cout << "Delete request for filename (decoded): " << filename << std::endl;
            // 从RaftKV中获取元数据
            auto fileid=filename_to_fileId_[filename];
            
            // 使用一致性哈希选择存储节点
            NodeInfo target_node = consistentHash_.getResponsibleNode(fileid);
            std::cout << "Selected storage node for delete: " << target_node.id << " (port: " << target_node.port << ")" << std::endl;
            
            // 直接使用NodeInfo中的conn
            auto storage_conn = target_node.conn;
            
            // 通过RPC调用存储节点的删除服务
            try {
                auto result = storage_conn->call<std::string>("delete_file", filename,fileid);
                std::string res_str = result.value();
                
                res->SetBody(res_str);
                return HTTP_STATUS_OK;
            } catch (const std::exception& e) {
                res->SetBody("RPC error: " + std::string(e.what()));
                return HTTP_STATUS_INTERNAL_SERVER_ERROR;
            }
        });
    }
    
private:
    // 获取缓存的HTML内容，支持热更新
    std::string getCachedHtml() {
        struct stat file_stat;
        if (stat("include/resource/index.html", &file_stat) == 0) {
            // 检查文件是否被修改
            if (file_stat.st_mtime > html_mtime_) {
                std::lock_guard<std::mutex> lock(html_mutex_);
                // 双重检查
                if (stat("include/resource/index.html", &file_stat) == 0 && 
                    file_stat.st_mtime > html_mtime_) {
                    std::ifstream html_file("include/resource/index.html");
                    if (html_file.is_open()) {
                        std::stringstream buffer;
                        buffer << html_file.rdbuf();
                        cached_html_ = buffer.str();
                        html_mtime_ = file_stat.st_mtime;
                    }
                }
            }
        }
        return cached_html_;
    }
    
    // 一致性哈希
    ConsistentHash& consistentHash_;
    
    // HTML缓存，支持热更新
    std::string cached_html_;
    std::mutex html_mutex_;
    time_t html_mtime_{0};

    // 从filename到fileId的映射
    std::unordered_map<std::string, std::string> filename_to_fileId_;
    

    std::thread updateNodeThread_;
};