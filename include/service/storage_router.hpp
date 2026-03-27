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
#include "qwen_stream_client.hpp"
#include "../../third_party/raftKV/include/kv_store.hpp"
#include "service/embedding_client.hpp"
#include "service/qwen_stream_client.hpp"
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
        if (updateNodeThread_.joinable()) {
            updateNodeThread_.join();
        }
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
            std::cout<<"filename = "<<filename<<" fileid = "<<fileId<<"\n";
            // 使用一致性哈希选择存储节点
            NodeInfo node = consistentHash_.getResponsibleNode(fileId);
            std::cout << "Selected storage node: " << node.id << " (port: " << node.port << ")" << std::endl;
            

             std::cout << "✅ 文件上传成功，开始自动构建 RAG 索引：" << filename << std::endl;

                // 1. 创建一个 RAG 对象（你可以改成全局/单例/Map 管理多个文件）
                auto rag_instance = std::make_unique<RAG>();
                rag_instance->load_document(file_content, filename);
                file_rag_map_[fileId] = std::move(rag_instance); // 移动指针，合法
                std::cout<<"filename = "<<filename<<" fileid = "<<fileId<<"\n";
                std::cout << "✅ RAG 索引构建完成：" << filename << " | fileId: " << fileId << std::endl;


                std::string summary = QwenClient::get().chat(
                    "请用100字以内概括这份文档的核心内容：\n" + file_content.substr(0, 2000)
                );
                file_info_map_[fileId] = {filename, summary};
                std::cout<<"filename = "<<filename<<" fileid = "<<fileId<<"\n";
                std::cout << "✅ 摘要生成成功：" << filename << std::endl;

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

        // 5. AI 聊天接口 (SSE)
        service_.POST("/api/chat", [this](const HttpContextPtr& ctx) -> int {
            // 解析请求体中的用户输入
            std::string user_input;
            try {
                auto req_json = nlohmann::json::parse(ctx->request->body);
                if (req_json.contains("message")) {
                    user_input = req_json["message"];
                }
            } catch (const std::exception& e) {
                ctx->response->SetBody("Invalid JSON request");
                return HTTP_STATUS_BAD_REQUEST;
            }

            if (user_input.empty()) {
                ctx->response->SetBody("Message is required");
                return HTTP_STATUS_BAD_REQUEST;
            }

            std::cout<<"user_input = "<<user_input<<"\n";
            ctx->writer->Begin();
            ctx->writer->WriteHeader("Cache-Control", "no-cache");
            ctx->writer->WriteHeader("Connection", "keep-alive");
            ctx->writer->EndHeaders("Content-Type", "text/event-stream");

            std::string router_prompt=build_router_prompt(user_input);
            std::cout<<"router_prompt = "<<router_prompt<<"\n";
            std::string fileid=QwenClient::get().chat(std::move(router_prompt));
            std::cout << "✅ 路由选择 : " << fileid << std::endl;
            auto chunks=file_rag_map_[fileid]->hybrid_search(user_input);

            std::string final_prompt = buildRagPrompt(user_input, chunks);
            QwenClient::get().run(final_prompt, [&ctx](const std::string& chunk, bool is_done) {
                if (is_done) {
                    ctx->writer->write("data: [DONE]\n\n");
                } else {
                    // 转义换行符等，为了简单，我们可以将增量文本包装进 JSON
                    nlohmann::json res_chunk;
                    res_chunk["content"] = chunk;
                    std::string data_str = "data: " + res_chunk.dump() + "\n\n";
                    ctx->writer->write(data_str);
                }
                return true;
            });

            ctx->writer->close();
            return 0; // 0 表示请求已在回调中处理完毕
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
    
    std::string build_router_prompt(const std::string& user_query) {
        std::string prompt = R"(
你是一个RAG路由专家，根据用户问题选择最相关的文档。
只输出fileId，不要输出任何多余内容。

可选知识库：
)";

        for (auto& [fid, info] : file_info_map_) {
            prompt += "fileId: " + fid + "\n";
            prompt += "文件名: " + info.first + "\n";
            prompt += "内容摘要: " + info.second + "\n\n";
        }

        prompt += "用户问题：" + user_query + "\n";
        prompt += "请输出匹配的fileId：";
        return prompt;
    }

    
    std::string buildRagPrompt(
        const std::string& user_input,
        const std::vector<std::pair<std::string, float>>& callback_chunks
    ) {
        // 系统角色定义
        std::string prompt = R"(
    你是一个专业的智能问答助手，请严格根据提供的参考资料回答用户问题，不要编造信息。
    如果参考资料中没有相关答案，请明确说明：“根据现有资料无法回答”。
    回答要求：准确、简洁、有条理、不使用格式化符号。

    【参考资料】
    )";

        // 拼接所有检索片段
        for (size_t i = 0; i < callback_chunks.size(); ++i) {
            prompt += "[" + std::to_string(i + 1) + "] " + callback_chunks[i].first + "\n";
        }

        // 最后加入用户问题
        prompt += "\n【用户问题】\n" + user_input + "\n";
        prompt += "\n请根据参考资料回答：\n";

        return prompt;
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
    std::unordered_map<std::string, std::unique_ptr<RAG>> file_rag_map_;
    // 存储结构：fileId → { 文件名, 摘要 }
    std::unordered_map<std::string, std::pair<std::string, std::string>> file_info_map_;
};