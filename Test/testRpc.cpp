#include <iostream>
#include <thread>
#include "mrpc/server.hpp"
#include "mrpc/client.hpp"

using namespace mrpc;

// RPC服务端函数
int test_add(int i, int j) {
    std::cout << "Server received: " << i << " + " << j << std::endl;
    return i + j;
}

// 启动RPC服务器
void start_server() {
    auto& server = server::get();
    server.set_ip_port("127.0.0.1", 3333);
    server.set_server_name("test_server");
    server.run();
    
    // 注册RPC函数
    server.reg_func("test_add", test_add);
    
    server.accept();
    server.wait_shutdown();
}

int main() {
    // 启动服务器线程
    std::thread server_thread(start_server);
    
    // 等待服务器启动
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    // 客户端连接
    auto& client = client::get();
    client.run();
    
    auto conn = client.connect("127.0.0.1", 3333);
    if (!conn) {
        std::cerr << "Failed to connect to server" << std::endl;
        return 1;
    }
    
    // 测试RPC调用
    std::cout << "Client calling test_add(5, 3)..." << std::endl;
    auto result = conn->call<int>("test_add", 5, 3);
    std::cout << "Result: 5 + 3 = " << result.value() << std::endl;
    
    // 测试另一个调用
    std::cout << "Client calling test_add(10, 20)..." << std::endl;
    result = conn->call<int>("test_add", 10, 20);
    std::cout << "Result: 10 + 20 = " << result.value() << std::endl;
    
    // 关闭客户端
    client.wait_shutdown();
    
    // 等待服务器线程结束
    server_thread.detach();
    
    std::cout << "RPC test completed!" << std::endl;
    
    return 0;
}