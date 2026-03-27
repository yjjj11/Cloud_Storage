// qwen_stream_client.cpp
#define CPPHTTPLIB_OPENSSL_SUPPORT  // 必须在包含头文件前定义
#include <httplib.h>
#include "httplib.h"  // 请确保此文件在你的项目目录中
#include "json.hpp"   // 请确保此文件在你的项目目录中
#include <iostream>
#include <string>

using json = nlohmann::json;

int main() {
    // --- 用户配置区域 ---
    // 1. 设置你的 DashScope API Key
    std::string api_key = "sk-d5011e9c33c64b46b2b9cb83f2856e20"; 

    // 2. 设置要使用的模型名称 (例如: qwen-max, qwen-plus, qwen-turbo, qwen2.5-72b-instruct 等)
    std::string model_name = "qwen-plus";

    // 3. 设置你的输入提示 (prompt)
    std::string user_input = "你好，请介绍一下你自己。";
    // --- 配置区域结束 ---

    // 构建 HTTP 客户端
    httplib::Client cli("https://dashscope.aliyuncs.com");

    // 设置请求头
    httplib::Headers headers;
    headers.insert({"Authorization", "Bearer " + api_key}); // 使用 Bearer Token 格式
    headers.insert({"X-DashScope-SSE", "enable"}); // 启用 Server-Sent Events (SSE) 流式
    headers.insert({"Accept", "text/event-stream"}); // 关键！设置 Accept 为 text/event-stream

    // 构建 JSON 请求体
    json req_body_json;
    req_body_json["model"] = model_name;
    req_body_json["input"]["messages"] = {{{"role", "user"}, {"content", user_input}}};
    req_body_json["parameters"]["stream"] = true; // 开启流式返回
    std::string req_body = req_body_json.dump();

    std::cout << "Sending request to Qwen...\n\n";

    // --- 优化的流式接收数据回调函数 ---
    // 使用一个成员变量来存储之前收到的完整文本
    std::string accumulated_text = "";

    auto stream_callback = [&](const char *data, size_t data_length) {
    std::string chunk(data, data_length);

    // 解析 JSON 数据
    json j;
    try {
        j = json::parse(chunk);
        if (j.contains("data") && j["data"].contains("output")) {
            auto output = j["data"]["output"];
            if (output.contains("text")) {
                std::string new_text = output["text"];
                
                if (new_text.length() > accumulated_text.length()) {
                    std::string added_part = new_text.substr(accumulated_text.length());
                    std::cout << added_part << std::flush; // 关键！强制刷新
                    accumulated_text = new_text;
                }
            }
        }
    } catch (json::exception &e) {
        // 忽略非 JSON 数据
    }

    return true;
};
    // --- 回调函数结束 ---

    // 发起 POST 请求
    auto res = cli.Post("/api/v1/services/aigc/text-generation/generation", 
                        headers, 
                        req_body, 
                        "application/json", 
                        stream_callback);

    // 检查请求结果
    if (res) {
        if (res->status == 200) {
            std::cout << "\n\n--- Stream finished successfully ---" << std::endl;
        } else {
            std::cout << "\n\n--- Request failed ---" << std::endl;
            std::cout << "Status Code: " << res->status << std::endl;
            std::cout << "Response Body: " << res->body << std::endl;
        }
    } else {
        auto error = res.error();
        std::cout << "\n\n--- Network error occurred ---" << std::endl;
        std::cout << "Error: " << httplib::to_string(error) << std::endl;
    }

    return 0;
}