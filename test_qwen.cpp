#include "include/service/qwen_stream_client.hpp"
#include <iostream>

int main() {
    std::cout << "You: 你好，请介绍一下你自己。" << std::endl;
    std::cout << "AI: ";
    std::cout.flush(); // 关键：强制刷新缓冲区，确保先打印 "AI: "

    // 流式实时输出
    QwenClient::get().run("你好，请介绍一下你自己。", [](const std::string& chunk, bool done) {
        if (!done) {
            std::cout << chunk;
            std::cout.flush(); // 强制刷新，确保每次回调的内容都立刻打印
        } else {
            std::cout << std::endl;
        }
        return true;
    });

    return 0;
}