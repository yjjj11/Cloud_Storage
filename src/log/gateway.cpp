#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <memory>
#include <vector>
#include <iostream>
#include <mrpc/client.hpp>
#include "../include/service/route.hpp"

using namespace mrpc;

class GatewayServer {
public:
    GatewayServer(int port, int thread_num) 
        : port_(port), thread_num_(thread_num) {
        initRpcClient();
        registerRoutes();

        server_.registerHttpService(&service_);
        server_.setPort(port_);
        server_.setThreadNum(thread_num_);
        std::cout << "Gateway running on port " << port_ << std::endl;
    }
    
    void start() {
        server_.run();
    }
    
    // 获取RPC连接
    std::shared_ptr<connection> getStorageConnection() {
        return storage_conn_;
    }
    
private:
    void initRpcClient() {
        // 初始化RPC客户端
        client_ = &client::get();
        client_->run();
        
        // 连接到存储节点
        // 这里假设存储节点运行在本地3333端口
        storage_conn_ = client_->connect("127.0.0.1", 3333);
        if (!storage_conn_) {
            std::cerr << "Failed to connect to storage node" << std::endl;
        } else {
            std::cout << "Connected to storage node" << std::endl;
        }
    }
    
    void registerRoutes() {
        // 注册存储路由，传入RPC连接
        routes_.emplace_back(std::make_unique<StorageRouter>(service_, getStorageConnection()));
        for (auto& router : routes_) {
            router->RegisterRoute();
        }
    }

    int port_{0};
    int thread_num_{0};
    hv::HttpService service_;
    hv::HttpServer server_;
    std::vector<std::unique_ptr<Router>> routes_;
    
    // RPC客户端和连接
    client* client_;
    std::shared_ptr<connection> storage_conn_;
};

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    
    GatewayServer gateway(port, 4);
    gateway.start();
    
    return 0;
}