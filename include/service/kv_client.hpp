#ifndef KV_CLIENT_HPP
#define KV_CLIENT_HPP
#include <iostream>
#include <string>
#include <mrpc/client.hpp>
#include <nlohmann/json.hpp>

using namespace mrpc;
using json = nlohmann::json;

/**
 * KV 远程客户端封装
 * 通过RPC调用元数据节点的Put/Get/Del接口
 */
class KvClient {
public:
    KvClient(const std::string& meta_ip, int meta_port)
        : meta_ip_(meta_ip), meta_port_(meta_port) {  // KV RPC端口=Raft端口+1
    }

    // 远程Put
    bool Put(const std::string& key, const std::string& value) {
        try {
            auto conn = mrpc::client::get().connect(meta_ip_, meta_port_, 1000);
            if (conn) {
                auto result = conn->call<bool>("kv_put", key, value);
                // std::cout<<"对9001调用了Put(存储节点上线)\n";
                return result.error_code() == mrpc::ok && result.value();
            }
        } catch (const std::exception& e) {
            std::cout << "[KvClient] Put error: " << e.what() << std::endl;
        }
        return false;
    }

    // 远程Get
    std::string Get(const std::string& key) {
        try {
            auto conn = mrpc::client::get().connect(meta_ip_, meta_port_, 1000);
            if (conn) {
                auto result = conn->call<std::string>("kv_get", key);
                if (result.error_code() == mrpc::ok) {
                    return result.value();
                }
            }
        } catch (const std::exception& e) {
            std::cout << "[KvClient] Get error: " << e.what() << std::endl;
        }
        return "";
    }

    // 远程Del
    bool Del(const std::string& key) {
        try {
            auto conn = mrpc::client::get().connect(meta_ip_, meta_port_, 100);
            if (conn) {
                auto result = conn->call<bool>("kv_del", key);
                return result.error_code() == mrpc::ok && result.value();
            }
        } catch (const std::exception& e) {
            std::cout << "[KvClient] Del error: " << e.what() << std::endl;
        }
        return false;
    }

private:
    std::string meta_ip_;
    int meta_port_;
};

#endif // KV_CLIENT_HPP
