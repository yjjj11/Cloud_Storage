#ifndef QWEN_STREAM_CLIENT_HPP
#define QWEN_STREAM_CLIENT_HPP

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <functional>

using json = nlohmann::json;

class QwenClient {
public:
    QwenClient(){}
    static QwenClient& get(){
        static QwenClient client;
        return client;
    }
    // 暴露的 run 接口，接受提示词和流式回调函数
    // callback 参数：(新生成的文本块, 是否完成) -> 返回 true 继续，返回 false 终止
    bool run(const std::string& user_input, std::function<bool(const std::string&, bool)> callback) {
        httplib::Client cli("https://dashscope.aliyuncs.com");
        cli.set_read_timeout(120, 0); // 设置较长的超时时间

        httplib::Headers headers;
        headers.insert({"Authorization", "Bearer " + api_key_});
        headers.insert({"X-DashScope-SSE", "enable"});
        headers.insert({"Accept", "text/event-stream"});

        json req_body_json;
        req_body_json["model"] = model_name_;
        req_body_json["input"]["messages"] = {{{"role", "user"}, {"content", user_input}}};
        req_body_json["parameters"]["stream"] = true;
        std::string req_body = req_body_json.dump();

        std::string accumulated_text = "";

        auto stream_callback = [&](const char *data, size_t data_length) {
            std::string chunk(data, data_length);

            // 查找所有 "data:" 前缀的行
            size_t pos = 0;
            while ((pos = chunk.find("data:", pos)) != std::string::npos) {
                pos += 5; // skip "data:"
                size_t end_pos = chunk.find("\n", pos);
                std::string json_str = chunk.substr(pos, end_pos - pos);
                
                // 去除首尾空格
                json_str.erase(0, json_str.find_first_not_of(" \r\t"));
                json_str.erase(json_str.find_last_not_of(" \r\t") + 1);

                if (!json_str.empty()) {
                    try {
                        json j = json::parse(json_str);
                        if (j.contains("output") && j["output"].contains("text")) {
                            std::string new_text = j["output"]["text"];
                            
                            if (new_text.length() > accumulated_text.length()) {
                                std::string added_part = new_text.substr(accumulated_text.length());
                                accumulated_text = new_text;
                                // 调用回调传递增量内容
                                if (!callback(added_part, false)) {
                                    return false; // 终止接收
                                }
                            }
                        }
                    } catch (json::exception &e) {
                        // 忽略非 JSON 数据
                    }
                }
            }
            return true;
        };

        auto res = cli.Post("/api/v1/services/aigc/text-generation/generation", 
                            headers, 
                            req_body, 
                            "application/json", 
                            stream_callback);

        if (res && res->status == 200) {
            callback("", true); // 触发完成状态
            return true;
        } else {
            if (res) {
                std::cerr << "Qwen API Request failed, Status Code: " << res->status << std::endl;
                std::cerr << "Response Body: " << res->body << std::endl;
            } else {
                std::cerr << "Qwen API Network error occurred: " << httplib::to_string(res.error()) << std::endl;
            }
            callback("", true); // 触发完成状态
            return false;
        }
    }

    // ==============================
    // 🔥 新增：非流式接口（专门给 RAG 路由用）
    // 返回：LLM 回答的字符串
    // ==============================
    std::string chat(const std::string& prompt) {
        std::string res;
        QwenClient::get().run(prompt, [&](const std::string& chunk, bool done) {
            res += chunk;
            return true;
        });
        return res;
    }
    


private:
    std::string api_key_ = "sk-d5011e9c33c64b46b2b9cb83f2856e20"; 
    std::string model_name_ = "qwen-plus";
};

#endif // QWEN_STREAM_CLIENT_HPP
