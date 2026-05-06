#ifndef GATEWAY_HPP
#define GATEWAY_HPP

#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <memory>
#include <vector>
#include <mrpc/client.hpp>
#include <mrpc/server.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include "storage_router.hpp"
#include "hash.hpp"
#include "kv_client.hpp"

using namespace mrpc;

class GatewayServer {
public:
    GatewayServer(int http_port, const std::string& meta_ip, int meta_port)
        : http_port_(http_port)
        , kv_client_(meta_ip, meta_port)
        , storage_router_(service_, consistentHash_, meta_ip, meta_port) {

        std::cout << "[Gateway] 正在初始化..." << std::endl;

        // 启动RPC客户端（用于调用存储节点和元数据节点）
        mrpc::client::get().run(1, 1);

        // 启动网关的RPC回调服务（端口=HTTP端口+1000）
        int gateway_rpc_port = http_port_ + 1000;
        auto& srv = mrpc::server::get();
        srv.set_ip_port("127.0.0.1", gateway_rpc_port);
        srv.run(1, 1);
        srv.accept();

        // 注册RPC回调（接收元数据节点的节点上下线通知）
        srv.reg_func("add_storage_node",
            [this](const std::string& nodeId, int port) -> std::string {
                std::cout << "[Gateway] 收到节点上线通知: " << nodeId << ":" << port << std::endl;
                auto conn = client::get().connect("127.0.0.1", port, 100);
                if (conn) {
                    consistentHash_.addNode(nodeId, port, conn);
                    std::cout << "[Gateway] 节点已加入哈希环: " << nodeId << std::endl;
                    return "ok";
                }
                return "error: connect failed";
            });

        srv.reg_func("remove_storage_node",
            [this](const std::string& nodeId, int port) -> std::string {
                std::cout << "[Gateway] 收到节点下线通知: " << nodeId << std::endl;
                consistentHash_.removeNode(nodeId, port);
                std::cout << "[Gateway] 节点已从哈希环移除: " << nodeId << std::endl;
                return "ok";
            });

        // 向元数据节点注册自己（这样元数据节点才能通知我们节点上下线）
        std::cout << "[Gateway] 向元数据节点注册网关回调地址..." << std::endl;
        auto conn = client::get().connect(meta_ip, meta_port, 100);
        if (conn) {
            // 网关的RPC回调端口 = HTTP端口 + 1000（避免端口冲突）
            int gateway_rpc_port = http_port_ + 1000;
            auto result = conn->call<bool>("register_gateway", std::string("127.0.0.1"), gateway_rpc_port);
            if (result.error_code() == mrpc::ok && result.value()) {
                std::cout << "[Gateway] 网关注册成功！RPC回调端口: " << gateway_rpc_port << std::endl;
            } else {
                std::cout << "[Gateway] 警告: 网关注册失败" << std::endl;
            }
        }

        // 从元数据节点拉取已注册的存储节点，初始化哈希环
        std::cout << "[Gateway] 拉取全部存储节点...\n";
        if (conn) {
            auto res = conn->call<std::string>("get_all_storage_nodes",0);
            if (res.error_code() == mrpc::ok && !res.value().empty()) {
                std::stringstream ss(res.value());
                std::string port_str;
                while (getline(ss, port_str, ',')) {
                    if (port_str.empty()) continue;
                    int port = stoi(port_str);
                    auto node_conn = client::get().connect("127.0.0.1", port, 100);
                    if (node_conn) {
                        consistentHash_.addNode("node" + port_str, port, node_conn);
                        std::cout << "[Gateway] 加载节点 port:" << port << "\n";
                    }
                }
            }
        }

        // 注册HTTP路由
        storage_router_.RegisterRoute();
        server_.registerHttpService(&service_);
        server_.setPort(http_port_);
    }

    void start() {
        std::cout << "[Gateway] HTTP服务启动，端口: " << http_port_ << std::endl;
        server_.run();
    }

private:
    int http_port_;
    KvClient kv_client_;
    hv::HttpService service_;
    hv::HttpServer server_;
    ConsistentHash consistentHash_;
    StorageRouter storage_router_;
};

#endif // GATEWAY_HPP
