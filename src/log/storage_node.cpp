/**
 * 存储节点 (Storage Node) - 纯文件存储服务
 *
 * 职责：
 * 1. 提供文件上传/下载/列举/删除的RPC服务
 * 2. 启动时向元数据节点注册自己
 * 3. 关闭时从元数据节点注销自己
 *
 * 不再内嵌RaftKV，通过RPC调用元数据节点
 */
#include <iostream>
#include <thread>
#include <mrpc/server.hpp>
#include <mrpc/client.hpp>
#include <fstream>
#include <dirent.h>
#include <sys/stat.h>
#include <signal.h>
#include "hash.hpp"
#include "kv_client.hpp"
#include "service/embedding_client.hpp"
#include "service/qwen_stream_client.hpp"
using namespace mrpc;

// 全局配置
std::string STORAGE_DIR;
std::string NODE_ID;
int STORAGE_PORT = 0;
std::string META_IP = "127.0.0.1";
int META_PORT = 9000;

std::unique_ptr<KvClient> g_kvClient = nullptr;
std::unordered_map<std::string, std::unique_ptr<RAG>> file_rag_map_;
// 存储结构：fileId → { 文件名, 摘要 }
std::unordered_map<std::string, std::pair<std::string, std::string>> file_info_map_;

// 确保目录存在
void ensure_directory(const std::string &dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        mkdir(dir.c_str(), 0755);
    }
}

// ============ RPC服务端函数 ============

// 上传文件
std::string upload_file(const CloudStorageMetadata &metadata, const std::string &content) {
    std::string filepath = STORAGE_DIR + "/" + metadata.filename;
    std::ofstream file(filepath, std::ios::binary);
    if (file.is_open()) {
        file << content;
        file.close();
        std::cout << "[StorageNode] 文件上传成功: " << metadata.filename
                  << " (fileId: " << metadata.fileId << ")" << std::endl;

        // 把元数据存到元数据节点
        json j = metadata;
        g_kvClient->Put("metadata:" + metadata.fileId, j.dump());
        g_kvClient->Put("filemap:" + metadata.filename, metadata.fileId);

        std::cout << "✅ 文件上传成功，开始自动构建 RAG 索引：" << metadata.filename << std::endl;

        // 1. 构建 RAG 索引
        auto rag_instance = std::make_unique<RAG>();
        rag_instance->load_document(content, metadata.filename);
        file_rag_map_[metadata.fileId] = std::move(rag_instance);
        std::cout << "✅ RAG 索引构建完成：" << metadata.filename << " | fileId: " << metadata.fileId << std::endl;

        // 2. 生成摘要
        std::string summary;
        try {
            // 安全截断，避免超长内容导致LLM报错
            std::string short_content = content.substr(0, 3000);
            summary = QwenClient::get().chat(
                "请用100字以内概括这份文档的核心内容：\n" + short_content
            );
        } catch (const std::exception& e) {
            std::cerr << "[StorageNode] 摘要生成失败: " << e.what() << std::endl;
            summary = "摘要生成失败（非UTF-8编码文件或格式错误）";
        }

        // ==========================
        // ✅ 关键：把摘要写入本地文件
        // ==========================
        std::string summary_filepath = filepath + ".summary";
        std::ofstream summary_file(summary_filepath, std::ios::binary);
        if (summary_file.is_open()) {
            summary_file << summary;
            summary_file.close();
            std::cout << "✅ 摘要已保存到本地: " << summary_filepath << std::endl;
        } else {
            std::cerr << "❌ 摘要保存失败: " << summary_filepath << std::endl;
        }

        // 同时也存在内存，方便聊天路由快速读取
        file_info_map_[metadata.fileId] = {metadata.filename, summary};

        return "ok: " + metadata.fileId;
    }
    return "error: failed to save file";
}

// 下载文件
std::string download_file(const std::string &filename) {
    std::cout << "[StorageNode] 下载文件: " << filename << std::endl;
    std::string filepath = STORAGE_DIR + "/" + filename;
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return "";

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::string content;
    content.resize(size);
    file.read(&content[0], size);
    file.close();
    return content;
}

// 列举文件
std::string list_files(int dummy) {
    std::cout<<"[StorageNode] 列举文件" << std::endl;
    std::string result;
    DIR* dir = opendir(STORAGE_DIR.c_str());
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (entry->d_type == DT_REG) {
                result += std::string(entry->d_name) + "\n";
            }
        }
        closedir(dir);
    }
    return result;
}

// 删除文件
std::string delete_file(const std::string &filename, const std::string &fileid) {
    std::string filepath = STORAGE_DIR + "/" + filename;
    if (remove(filepath.c_str()) == 0) {
        std::cout << "[StorageNode] 文件删除成功: " << filename << std::endl;
        g_kvClient->Del("metadata:" + fileid);
        g_kvClient->Del("filemap:" + filename);
        return "ok: " + filename;
    }
    return "error: failed to delete file";
}

// ============ 信号处理：优雅关闭 ============

void signal_handler(int sig) {
    std::cout << "\n[StorageNode] 收到信号 " << sig << "，准备优雅关闭..." << std::endl;

    // 从元数据节点注销自己
    g_kvClient->Del("NodePort:" + NODE_ID);
    std::cout << "[StorageNode] 已从元数据节点注销" << std::endl;

    exit(0);
}


std::string get_all_summaries(int dummy) {
    json j;
    // 遍历本节点所有文件摘要
    for (auto& [fileId, info] : file_info_map_) {
        json item;
        item["fileId"] = fileId;
        item["filename"] = info.first;
        item["summary"] = info.second;
        j.push_back(item);
    }
    return j.dump();
}

std::vector<std::pair<std::string, float>> get_chunks(const std::string& fileId,std::string query) {
    std::cout << "[StorageNode] 获取文件块: " << fileId << " | query: " << query << std::endl;
    auto it = file_rag_map_.find(fileId);
    if (it == file_rag_map_.end()) {
        return {};
    }
    auto& rag_instance = it->second;
    auto chunks = rag_instance->hybrid_search(query);

    return chunks;
}

// ============ 【核心持久化】启动时从磁盘恢复索引和摘要 ============
void load_from_disk(const std::string& storage_dir) {
    DIR* dir = opendir(storage_dir.c_str());
    if (!dir) return;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename = entry->d_name;
        if (filename == "." || filename == "..") continue;

        // 跳过摘要文件，只处理原始文件
        if (filename.ends_with(".summary")) continue;

        std::string filepath = storage_dir + "/" + filename;
        std::string summary_path = filepath + ".summary";

        // 1. 读取文件内容（用于重建 RAG）
        std::ifstream file(filepath, std::ios::binary);
        if (!file) continue;

        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        // 2. 读取摘要
        std::string summary = "摘要未生成";
        std::ifstream summary_file(summary_path);
        if (summary_file) {
            summary = std::string((std::istreambuf_iterator<char>(summary_file)), std::istreambuf_iterator<char>());
        }

        // 3. 从元数据获取 fileId（必须！）
        std::string fileId = g_kvClient->Get("filemap:" + filename);
        if (fileId.empty()) {
            std::cerr << "⚠️ 未找到fileId: " << filename << std::endl;
            continue;
        }

        // 4. 重建 RAG 索引
        auto rag = std::make_unique<RAG>();
        rag->load_document(content, filename);

        // 5. 恢复到内存
        file_rag_map_[fileId] = std::move(rag);
        file_info_map_[fileId] = {filename, summary};

        std::cout << "[持久化恢复] " << filename << " → fileId: " << fileId << std::endl;
    }
    closedir(dir);
}


// ============ 主函数 ============
int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cout << "用法: " << argv[0] << " <node_id> <storage_port> <meta_port>" << std::endl;
        std::cout << "示例: " << argv[0] << " 1 8001 9000" << std::endl;
        return 1;
    }

    NODE_ID = argv[1];
    STORAGE_PORT = std::stoi(argv[2]);
    META_PORT = std::stoi(argv[3]);
    STORAGE_DIR = "storage_" + std::to_string(STORAGE_PORT);

    std::cout << "========================================" << std::endl;
    std::cout << "   存储节点 (Storage Node) 启动         " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Node ID:      " << NODE_ID << std::endl;
    std::cout << "  Storage Port: " << STORAGE_PORT << std::endl;
    std::cout << "  Meta Port:    " << META_PORT << std::endl;
    std::cout << "  Storage Dir:  " << STORAGE_DIR << std::endl;
    std::cout << "========================================" << std::endl;

    // 确保存储目录存在
    ensure_directory(STORAGE_DIR);

    // 启动RPC客户端（用于调用元数据节点）
    mrpc::client::get().run(1, 1);

    // 初始化KV客户端（连接元数据节点）
    g_kvClient = std::make_unique<KvClient>(META_IP, META_PORT);

    // 注册并启动RPC服务
    auto& srv = mrpc::server::get();
    srv.set_ip_port("127.0.0.1", STORAGE_PORT);
    srv.run(1, 1);
    srv.accept();
    srv.reg_func("upload_file", upload_file);
    srv.reg_func("download_file", download_file);
    srv.reg_func("list_files", list_files);
    srv.reg_func("delete_file", delete_file);
    srv.reg_func("get_all_summaries", get_all_summaries);
    srv.reg_func("get_chunks", get_chunks);
    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 向元数据节点注册自己
    std::cout << "[StorageNode] 正在向元数据节点注册..." << std::endl;
    bool ok = g_kvClient->Put("NodePort:" + NODE_ID, std::to_string(STORAGE_PORT));
    if (ok) {
        std::cout << "[StorageNode] 注册成功！" << std::endl;
    } else {
        std::cout << "[StorageNode] 注册失败！请检查元数据节点是否启动" << std::endl;
        return 1;
    }
    // ============ 这里加入！启动恢复 ============
    // load_from_disk(STORAGE_DIR);
    
    std::cout << "[StorageNode] 启动完成，等待文件请求..." << std::endl;

    // 保持运行
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}
