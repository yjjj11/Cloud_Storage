#include <iostream>
#include <thread>
#include <mrpc/server.hpp>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include "../include/log/AsyncLogger.hpp"
#include "../../third_party/raftKV/include/raftnode.hpp"
#include "../../third_party/raftKV/include/kv_store.hpp"
#include "../include/service/hash.hpp"
#include <nlohmann/json.hpp>
using namespace mrpc;
using json = nlohmann::json;

// 获取日志器
mylog::AsyncLogger::ptr getStorageLogger() {
    static mylog::LoggerBuilder builder;
    builder.BuildLoggerName("storage_node");
    builder.BuildLopperType(mylog::AsyncType::ASYNC_SAFE);
    builder.BuildLoggerFlush<mylog::StdoutFlush>();
    static auto logger = builder.Build();
    return logger;
}

// 存储目录（全局变量，将在main函数中设置）
std::string STORAGE_DIR = "storage";

// Raft节点全局变量
std::shared_ptr<RaftNode> g_raftNode = nullptr;
std::unique_ptr<KvService> g_kvService = nullptr;

// 获取存储目录（根据端口号）
std::string getStorageDir(int port) {
    return "storage_" + std::to_string(port);
}

// 确保目录存在
void ensure_directory(const std::string &dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        mkdir(dir.c_str(), 0755);
    }
}

// RPC服务端方法：上传文件
std::string upload_file(const CloudStorageMetadata &metadata, const std::string &content) {
    // 确保存储目录存在
    ensure_directory(STORAGE_DIR);
    
    // 存储文件
    std::string filepath = STORAGE_DIR + "/" + metadata.filename;
    std::ofstream file(filepath, std::ios::binary);
    if (file.is_open()) {
        file << content;
        file.close();
        std::cout << "File uploaded successfully: " << metadata.fileId << std::endl;
        // 存储元数据到RaftKV
        if (g_kvService) {
            std::string key = "metadata:" + metadata.fileId;
            json json = metadata;
            auto success = g_kvService->Put(key, json.dump());
            if (success) {
                std::cout << "Metadata submitted successfully for fileId: " << metadata.fileId << std::endl;    
            } else {
                std::cout << "Failed to submit metadata for fileId: " << metadata.fileId << std::endl;
            }
        }
        return "File uploaded successfully: " + metadata.fileId;
    } else {
        std::cout << "Failed to upload file: " << metadata.fileId << std::endl;
        return "Failed to upload file";
    }
}

// RPC服务端方法：下载文件
std::string download_file(const std::string &filename) {
    std::cout << "Downloading file: " << filename << std::endl;
    
    std::string filepath = STORAGE_DIR + "/" + filename;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cout << "File not found: " << filename << std::endl;
        return "";
    }
    
    // 使用tellg获取文件大小，预分配内存，提高性能
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::string content;
    content.resize(size);
    file.read(&content[0], size);
    file.close();
    
    std::cout << "File downloaded successfully: " << filename << std::endl;
    return content;
}

// RPC服务端方法：列举文件
std::string list_files(int dummy) {
    std::cout << "Listing files" << std::endl;
    
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
    std::cout << "Files listed" << std::endl;
    return file_list;
}

// RPC服务端方法：删除文件
std::string delete_file(const std::string &filename,const std::string &fileid) {
    std::cout << "Deleting file: " << filename << " fileId: " << fileid << std::endl;
    
    std::string filepath = STORAGE_DIR + "/" + filename;
    if (remove(filepath.c_str()) == 0) {
        std::cout << "File deleted successfully: " << filename << std::endl;
        return "File deleted successfully: " + filename;
    } else {
        std::cout << "Failed to delete file: " << filename << std::endl;
        return "Failed to delete file";
    }

    // 从RaftKV中删除元数据
    if (g_kvService) {
        std::string key = "metadata:" + fileid;
        g_kvService->Del(key);
        std::cout << "Metadata deleted from RaftKV for fileId: " << fileid << std::endl;
    }
}

int main(int argc, char* argv[]) {
    int port = 3333;  // 默认端口
    if (argc > 1) {
        port = std::stoi(argv[3]);
    }
    
    // 设置存储目录（每个节点使用不同目录）
    STORAGE_DIR = getStorageDir(port);
    
    std::cout << "Starting storage node on port " << port << std::endl;
    std::cout << "Storage directory: " << STORAGE_DIR << std::endl;
    
    // 初始化Raft节点
    try {
        g_raftNode = initialize_server(argc, argv);
        g_kvService = std::make_unique<KvService>(g_raftNode);
        std::cout << "Raft node initialized successfully" << std::endl;
    } catch (const std::exception& e) {
        std::cout << "Failed to initialize Raft node: " << e.what() << std::endl;
        return 1;
    }
    
    // 初始化RPC服务器
    auto& server = server::get();
    server.set_ip_port("127.0.0.1", port);
    server.set_server_name("storage_node");
    server.run();
    
    // 注册RPC方法
    server.reg_func("upload_file", upload_file);
    server.reg_func("download_file", download_file);
    server.reg_func("list_files", list_files);
    server.reg_func("delete_file", delete_file);
    
    std::this_thread::sleep_for(std::chrono::seconds(2));
    std::string nodelist = g_kvService->Get("StorageNode");
    nodelist[g_raftNode->node_id_]=1;
    g_kvService->Put("StorageNode", nodelist);//表明自己已经上线
    g_kvService->Put("NodePort:" + std::to_string(g_raftNode->node_id_), std::to_string(port));
    std::cout << "Storage node started on port " << port << std::endl;
    std::cout << "Registered RPC methods: upload_file, download_file, list_files, delete_file" << std::endl;
    
    // 启动监听
    server.accept();
    server.wait_shutdown();
    
    // 节点下线时，从KV中删除相应的键值对
    if (g_kvService && g_raftNode) {
        std::string nodeId = std::to_string(g_raftNode->node_id_);
        g_kvService->Del("NodePort:" + nodeId);
        std::cout << "Storage node offline: " << nodeId << " on port " << port << std::endl;
    }
    
    // 停止Raft节点
    if (g_raftNode) {
        g_raftNode->stop();
    }
    
    return 0;
}