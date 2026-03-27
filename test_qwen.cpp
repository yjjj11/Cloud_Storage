#include "include/service/qwen_stream_client.hpp"
#include <iostream>

int main() {
    QwenClient qwen_client = QwenClient::get();
    
    std::cout << "You: 你好，请介绍一下你自己。" << std::endl;
    std::cout << "AI: ";
    
    std::cout << QwenClient::get().chat("你好") << std::endl;
    return 0;
}