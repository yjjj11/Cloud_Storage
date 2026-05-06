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
#include "kv_client.hpp"
#include "qwen_stream_client.hpp"
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
    StorageRouter(HttpService &service, ConsistentHash& consistentHash,
                   const std::string& meta_ip, int meta_port)
        : Router(service)
        , consistentHash_(consistentHash)
        , kv_client_(meta_ip, meta_port) {
    }

    ~StorageRouter() = default;

public:
    void RegisterRoute() override {
        // 健康检查接口
        service_.GET("/health", [](HttpRequest* req, HttpResponse* res) -> int {
            res->SetBody("Server is running");
            return HTTP_STATUS_OK;
        });

        // 根路径HTML响应
        service_.GET("/", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string html = getCachedHtml();
            res->content_type = TEXT_HTML;
            res->SetBody(html);
            return HTTP_STATUS_OK;
        });

        // 1. 文件上传
        service_.POST("/api/storage/upload", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = urlDecode(req->GetParam("filename", ""));
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }

            // 优先用body参数获取纯内容（兼容测试）
            std::string body_param = req->GetParam("body", "");
            std::string file_content = body_param.empty() ? req->body : body_param;

            std::string fileId = generateFileId(filename);

            std::cout << "[Gateway] 上传文件: " << filename << " fileId: " << fileId << std::endl;

            // 一致性哈希选节点
            NodeInfo node = consistentHash_.getResponsibleNode(fileId);

            // 调用存储节点
            CloudStorageMetadata metadata;
            metadata.fileId = fileId;
            metadata.filename = filename;
            metadata.size = file_content.size();
            metadata.contentType = "application/octet-stream";
            metadata.bucketName = std::to_string(node.port);

            try {
                auto result = node.conn->call<std::string>("upload_file", metadata, file_content);
                res->SetBody(result.value());
                return HTTP_STATUS_OK;
            } catch (const std::exception& e) {
                res->SetBody("RPC error: " + std::string(e.what()));
                return HTTP_STATUS_INTERNAL_SERVER_ERROR;
            }
        });


        // 2. 文件下载（修改版：通过元数据中的 bucketName 找节点）
        service_.GET("/api/storage/download/{filename}", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = urlDecode(req->GetParam("filename", ""));
            if (filename.empty()) {
                res->SetBody("Filename is required");
                return HTTP_STATUS_BAD_REQUEST;
            }
            auto hash = generateFileId(filename);
            auto node = consistentHash_.getResponsibleNode(hash);

             std::cout << "[Gateway] 下载文件: " << filename << " fileId: " << hash << " 负责节点: " << node.port << std::endl;

             if (!node.conn) {
                res->SetBody("Responsible storage node is unavailable");
                return HTTP_STATUS_SERVICE_UNAVAILABLE;
            }

            // 6. 调用存储节点下载文件
            try {
                auto result = node.conn->call<std::string>("download_file", filename);
                std::string file_content = result.value();
                if (file_content.empty()) {
                    res->SetBody("File not found on storage node");
                    return HTTP_STATUS_NOT_FOUND;
                }
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
            std::cout << "[Gateway] 列举文件" << std::endl;
            std::string combined_list = "Files:\n";

            for (const auto& pair : consistentHash_.port_to_node_id_) {
                const NodeInfo& node = pair.second;
                std::cout << "[Gateway] 正在查询节点 " << node.port << " 的文件列表，连接状态: " << (node.conn ? "有效" : "无效") << std::endl;
                
                if (!node.conn) {
                    std::cout << "[Gateway] 节点 " << node.port << " 连接无效，跳过" << std::endl;
                    continue;
                }

                try {
                    // 调用无参数的 list_files
                    auto result = node.conn->call<std::string>("list_files");
                    if (result.error_code() != mrpc::ok) {
                        std::cout << "[Gateway] RPC调用失败: " << result.error_msg() << std::endl;
                        continue;
                    }

                    std::string files = result.value();
                    if (files.empty()) {
                        std::cout << "[Gateway] 节点 " << node.port << " 无文件" << std::endl;
                        continue;
                    }

                    std::cout << "[Gateway] 节点 " << node.port << " 返回文件: " << files << std::endl;
                    
                    // ====================== 关键修改 ======================
                    // 把每行前面加上 "- "，让前端能直接识别！
                    std::istringstream iss(files);
                    std::string line;
                    while (std::getline(iss, line)) {
                        if (!line.empty()) {
                            combined_list += "- " + line + "\n";
                        }
                    }
                    // ======================================================

                } catch (const std::exception& e) {
                    std::cout << "[Gateway] 节点 " << node.port << " 异常: " << e.what() << std::endl;
                }
            }

            res->SetBody(combined_list);
            return HTTP_STATUS_OK;
        });

        // 4. 删除文件
        service_.Delete("/api/storage/delete/{filename}", [this](HttpRequest* req, HttpResponse* res) -> int {
            std::string filename = urlDecode(req->GetParam("filename", ""));
            std::string fileId = kv_client_.Get("filemap:" + filename);
            if (fileId.empty()) {
                res->SetBody("File not found");
                return HTTP_STATUS_NOT_FOUND;
            }

            NodeInfo node = consistentHash_.getResponsibleNode(fileId);
            try {
                auto result = node.conn->call<std::string>("delete_file", filename, fileId);
                res->SetBody(result.value());
                return HTTP_STATUS_OK;
            } catch (const std::exception& e) {
                res->SetBody("RPC error: " + std::string(e.what()));
                return HTTP_STATUS_INTERNAL_SERVER_ERROR;
            }
        });


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

            auto node = consistentHash_.getResponsibleNode(fileid);
             if (!node.conn) {
                ctx->writer->write("data: " + nlohmann::json{{"error", "Responsible storage node is unavailable"}}.dump() + "\n\n");
                ctx->writer->close();
                return 0;
            }
            auto res=node.conn->call<std::vector<std::pair<std::string, float>>>("get_chunks", fileid, user_input);
            if(res.error_code()!=mrpc::ok){
                ctx->writer->write("data: " + nlohmann::json{{"error", "Failed to retrieve chunks from storage node"}}.dump() + "\n\n");
                ctx->writer->close();
                return 0;
            }
            auto chunks=res.value();
                std::cout << "✅ 从存储节点获取到 " << chunks.size() << " 个相关文本块" << std::endl;
            std::string final_prompt = buildRagPrompt(user_input, chunks);
            std::cout << "✅ 构建最终提示词:\n" << final_prompt << std::endl;
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
// ==============================
// 网关：从所有存储节点拉取摘要
// ==============================
    std::string build_router_prompt(const std::string& user_query) {
        std::string prompt = R"(
    你是一个RAG路由专家，根据用户问题选择最相关的文档。
    只输出fileId，不要输出任何多余内容。

    可选知识库：
    )";

        // 遍历所有存储节点，拉取每个节点的文件摘要
        for (const auto& pair : consistentHash_.port_to_node_id_) {
            const NodeInfo& node = pair.second;

            std::cout << "[Gateway] 查询节点 " << node.port << " 的文件摘要信息" << std::endl;
            if (!node.conn) {
                std::cout << "[Gateway] 节点 " << node.port << " 无效，跳过" << std::endl;
                continue;
            }

            try {
                // ======================
                // RPC 调用：获取摘要
                // ======================
                auto result = node.conn->call<std::string>("get_all_summaries",1);
                if (result.error_code() != mrpc::ok) {
                    std::cout << "[Gateway] RPC 获取摘要失败: " << result.error_msg() << std::endl;
                    continue;
                }

                std::string json_str = result.value();
                if (json_str.empty()) {
                    continue;
                }

                // 解析返回的 JSON 数组
                json summaries = json::parse(json_str);
                for (auto& item : summaries) {
                    std::string fileId = item["fileId"];
                    std::string filename = item["filename"];
                    std::string summary = item["summary"];

                    prompt += "fileId: " + fileId + "\n";
                    prompt += "文件名: " + filename + "\n";
                    prompt += "内容摘要: " + summary + "\n\n";
                }

            } catch (const std::exception& e) {
                std::cerr << "[Gateway] 节点 " << node.port << " 异常：" << e.what() << std::endl;
            }
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


        std::cout<<"构建RAG提示词:\n" << prompt << std::endl;
        return prompt;
    }

    std::string getCachedHtml() {
        struct stat file_stat;
        if (stat("include/resource/index.html", &file_stat) == 0) {
            if (file_stat.st_mtime > html_mtime_) {
                std::lock_guard<std::mutex> lock(html_mutex_);
                if (file_stat.st_mtime > html_mtime_) {
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

    ConsistentHash& consistentHash_;
    KvClient kv_client_;

    std::string cached_html_;
    std::mutex html_mutex_;
    time_t html_mtime_{0};
};
