/**
 * 元数据节点 (Meta Node) —— 【已修复：自动维护 storage:nodes】
 */
#include <iostream>
#include <thread>
#include <mrpc/server.hpp>
#include <mrpc/client.hpp>
#include "raftnode.hpp"
#include "kv_store.hpp"
#include <nlohmann/json.hpp>
#include <set>
#include <sstream>

using namespace mrpc;
using json = nlohmann::json;

// 全局变量
std::shared_ptr<RaftNode> g_raftNode = nullptr;
std::unique_ptr<KvService> g_kvService = nullptr;
std::vector<std::pair<std::string, int>> g_gateway_addrs;

// ============ 工具函数：维护全局节点列表 ============
void add_storage_node_to_list(int port) {
    std::string key = "storage:nodes";
    auto val = g_kvService->unsafe_get(key);
    std::set<std::string> ports;

    if (val.has_value() && !val->empty()) {
        std::stringstream ss(*val);
        std::string p;
        while (getline(ss, p, ',')) {
            if (!p.empty())
                ports.insert(p);
        }
    }

    ports.insert(std::to_string(port));
    std::string new_val;
    for (auto& p : ports) {
        if (!new_val.empty()) new_val += ",";
        new_val += p;
    }

    g_kvService->Put(key, new_val);
}

void remove_storage_node_from_list(int port) {
    std::string key = "storage:nodes";
    auto val = g_kvService->unsafe_get(key);
    if (!val.has_value()) return;

    std::set<std::string> ports;
    std::stringstream ss(*val);
    std::string p;
    while (getline(ss, p, ',')) {
        if (!p.empty())
            ports.insert(p);
    }

    ports.erase(std::to_string(port));
    std::string new_val;
    for (auto& p : ports) {
        if (!new_val.empty()) new_val += ",";
        new_val += p;
    }

    g_kvService->Put(key, new_val);
}

// ============ RPC服务端函数 ============
bool kv_put(const std::string& key, const std::string& value) {
    std::cout << "[MetaNode] kv_put: " << key << " = " << value << std::endl;
    bool ok = g_kvService->Put(key, value);

    // ====================== 【关键修复】 ======================
    // 写入 NodePort 时 → 自动加入全局节点列表
    if (ok && key.substr(0, 9) == "NodePort:") {
        int port = std::stoi(value);
        add_storage_node_to_list(port);
    }

    return ok;
}

std::string kv_get(const std::string& key) {
    auto res = g_kvService->unsafe_get(key);
    return res.has_value() ? res.value() : "";
}

bool kv_del(const std::string& key) {
    std::cout << "[MetaNode] kv_del: " << key << std::endl;

    int port = -1;
    // ====================== 【关键修复】 ======================
    // 删除 NodePort 前 → 先拿到端口
    if (key.substr(0, 9) == "NodePort:") {
        auto val = g_kvService->unsafe_get(key);
        if (val.has_value()) {
            port = std::stoi(val.value());
        }
    }

    bool ok = g_kvService->Del(key);

    // 删除成功 → 从全局列表移除
    if (ok && port != -1) {
        remove_storage_node_from_list(port);
    }

    return ok;
}

bool register_gateway(const std::string& gateway_ip, int gateway_port) {
    std::cout << "[MetaNode] 注册网关: " << gateway_ip << ":" << gateway_port << std::endl;
    std::string key = "gateway:" + gateway_ip;
    std::string val = std::to_string(gateway_port);
    return g_kvService->Put(key, val);
}

// ============ 获取所有存储节点（逗号分隔端口） ============
std::string get_all_storage_nodes(int dummy) {
    auto val = g_kvService->unsafe_get("storage:nodes");
    return val.has_value() ? val.value() : "";
}

// ============ 通知网关 ============
void notify_gateways_node_up(const std::string& nodeId, int port) {
    for (auto& [ip, p] : g_gateway_addrs) {
        try {
            auto c = client::get().connect(ip, p, 100);
            if (c)
                c->call<std::string>("add_storage_node", nodeId, port);
        } catch (...) {}
    }
}

void notify_gateways_node_down(const std::string& nodeId) {
    for (auto& [ip, p] : g_gateway_addrs) {
        try {
            auto c = client::get().connect(ip, p, 100);
            if (c)
                c->call<std::string>("remove_storage_node", nodeId, 0);
        } catch (...) {}
    }
}

// ============ main ============
int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cout << "用法: " << argv[0] << " <node_id> <ip> <rpc_port> <election_timeout>\n";
        return 1;
    }

    g_raftNode = initialize_server(argc, argv);
    g_kvService = std::make_unique<KvService>(g_raftNode);

    int kv_port = std::stoi(argv[3]);
    auto& srv = server::get();
    srv.set_ip_port(argv[2], kv_port);
    srv.run(1, 1);
    srv.accept();

    srv.reg_func("kv_put", kv_put);
    srv.reg_func("kv_get", kv_get);
    srv.reg_func("kv_del", kv_del);
    srv.reg_func("register_gateway", register_gateway);
    srv.reg_func("get_all_storage_nodes", get_all_storage_nodes);

    // 监听节点上下线并通知网关
    g_kvService->WATCH("put", "node_up", [](const std::string& key, const std::string& value) {
        if (key.substr(0, 9) == "NodePort:") {
            notify_gateways_node_up(key.substr(9), std::stoi(value));
        }
        if (key.substr(0, 8) == "gateway:") {
            std::cout<<"[MetaNode] 发现网关注册: " << key.substr(8) << ":" << value << std::endl;
            g_gateway_addrs.emplace_back(key.substr(8), std::stoi(value));
        }
        return true;
    });

    g_kvService->WATCH("Del", "node_down", [](const std::string& key) {
        if (key.substr(0, 9) == "NodePort:") {
            notify_gateways_node_down(key.substr(9));
        }
        return true;
    });

    std::cout << "[MetaNode] 启动成功！KV端口: " << kv_port << "\n";
    while (true)
        std::this_thread::sleep_for(std::chrono::seconds(1));
    return 0;
}