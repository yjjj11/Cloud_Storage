/**
 * 网关入口程序
 * 不再内嵌Raft节点，作为纯HTTP+RPC客户端连接元数据节点
 */
#include <iostream>
#include "gateway.hpp"

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cout << "用法: " << argv[0] << " <http_port> <meta_port>" << std::endl;
        std::cout << "示例: " << argv[0] << " 8080 9000" << std::endl;
        return 1;
    }

    int http_port = std::stoi(argv[1]);
    int meta_port = std::stoi(argv[2]);

    std::cout << "========================================" << std::endl;
    std::cout << "        网关 (Gateway) 启动             " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  HTTP Port:    " << http_port << std::endl;
    std::cout << "  Meta Port:    " << meta_port << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        GatewayServer gateway(http_port, "127.0.0.1", meta_port);
        std::cout << "[Gateway] 启动完成！" << std::endl;
        gateway.start();
    } catch (const std::exception& e) {
        std::cout << "[Gateway] 启动失败: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
