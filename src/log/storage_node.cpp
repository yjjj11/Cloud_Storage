#include <iostream>
#include <thread>
#include <mrpc/server.hpp>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>

using namespace mrpc;

// 存储目录
const std::string STORAGE_DIR = "storage";

// 确保目录存在
void ensure_directory(const std::string &dir) {
    mkdir(dir.c_str(), 0755);
}

// RPC服务端方法：上传文件
std::string upload_file(const std::string &filename, const std::string &content) {
    // 确保存储目录存在
    ensure_directory(STORAGE_DIR);
    
    // 存储文件
    std::string filepath = STORAGE_DIR + "/" + filename;
    std::ofstream file(filepath, std::ios::binary);
    if (file.is_open()) {
        file << content;
        file.close();
        return "File uploaded successfully: " + filename;
    } else {
        return "Failed to upload file";
    }
}

// RPC服务端方法：下载文件
std::string download_file(const std::string &filename) {
    std::string filepath = STORAGE_DIR + "/" + filename;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        return "";
    }
    
    // 读取文件内容
    std::string content((std::istreambuf_iterator<char>(file)), 
                         std::istreambuf_iterator<char>());
    file.close();
    return content;
}

// RPC服务端方法：列举文件
std::string list_files(int dummy) {
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
    return file_list;
}

// RPC服务端方法：删除文件
std::string delete_file(const std::string &filename) {
    std::string filepath = STORAGE_DIR + "/" + filename;
    if (remove(filepath.c_str()) == 0) {
        return "File deleted successfully: " + filename;
    } else {
        return "Failed to delete file";
    }
}

int main() {
    std::cout << "Starting storage node..." << std::endl;
    
    // 初始化RPC服务器
    auto& server = server::get();
    server.set_ip_port("127.0.0.1", 3333);
    server.set_server_name("storage_node");
    server.run();
    
    // 注册RPC方法
    server.reg_func("upload_file", upload_file);
    server.reg_func("download_file", download_file);
    server.reg_func("list_files", list_files);
    server.reg_func("delete_file", delete_file);
    
    std::cout << "Storage node started on port 3333" << std::endl;
    std::cout << "Registered RPC methods: upload_file, download_file, list_files, delete_file" << std::endl;
    
    // 启动监听
    server.accept();
    server.wait_shutdown();
    
    return 0;
}