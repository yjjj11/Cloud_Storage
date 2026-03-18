#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <memory>
#include <vector>
#include <iostream>
#include "../include/service/route.hpp"

class MyHttpServer {
public:
    // 构造函数实现
    MyHttpServer(int port, int thread_num) 
        : port_(port), thread_num_(thread_num) {
        registerRoutes();

        server_.registerHttpService(&service_);
        server_.setPort(port_);
        server_.setThreadNum(thread_num_);
        std::cout << "Server running on port " << port_ << std::endl;
    }
    void start() {
        server_.run();
    }
private:
    void registerRoutes() {
        routes_.emplace_back(std::make_unique<StorageRouter>(service_));
        for (auto& router : routes_) {
            router->RegisterRoute();
        }
    }

    int port_{0};
    int thread_num_{0};
    hv::HttpService service_;
    hv::HttpServer server_;
    std::vector<std::unique_ptr<Router>> routes_;
};

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }
    MyHttpServer server(port, 4);
    server.start();
    return 0;
}
