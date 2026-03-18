#ifndef GATEWAY_HPP
#define GATEWAY_HPP

#include <hv/HttpServer.h>
#include <hv/HttpService.h>
#include <memory>
#include <vector>
#include <mrpc/client.hpp>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include "storage_router.hpp"
#include "hash.hpp"
#include "../log/AsyncLogger.hpp"

using namespace mrpc;

// 获取日志器
inline mylog::AsyncLogger::ptr getGatewayLogger() {
    static mylog::LoggerBuilder builder;
    builder.BuildLoggerName("gateway");
    builder.BuildLopperType(mylog::AsyncType::ASYNC_SAFE);
    builder.BuildLoggerFlush<mylog::StdoutFlush>();
    static auto logger = builder.Build();
    return logger;
}

// 配置管理器
class ConfigManager {
public:
    ConfigManager(const std::string& configFile) {
        loadConfig(configFile);
    }
    
    int getInt(const std::string& key, int defaultValue = 0) {
        auto it = configs_.find(key);
        if (it != configs_.end()) {
            try {
                return std::stoi(it->second);
            } catch (...) {
                return defaultValue;
            }
        }
        return defaultValue;
    }
    
    std::vector<int> getIntList(const std::string& key) {
        std::vector<int> result;
        auto it = configs_.find(key);
        if (it != configs_.end()) {
            std::stringstream ss(it->second);
            std::string token;
            while (std::getline(ss, token, ',')) {
                try {
                    result.push_back(std::stoi(token));
                } catch (...) {
                    // 忽略无效值
                }
            }
        }
        return result;
    }
    
private:
    void loadConfig(const std::string& configFile) {
        std::ifstream file(configFile);
        if (!file.is_open()) {
            std::cerr << "Failed to open config file: " << configFile << std::endl;
            return;
        }
        
        std::string line;
        while (std::getline(file, line)) {
            // 忽略注释和空行
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                // 去除首尾空格
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                configs_[key] = value;
            }
        }
        file.close();
    }
    
    std::unordered_map<std::string, std::string> configs_;
};

class GatewayServer {
public:
    GatewayServer(const std::string& configFile = "config/gateway_config.env", std::shared_ptr<KvService> kvService = nullptr) {
        ConfigManager config(configFile);
        kvService_ = kvService;
        port_ = config.getInt("GATEWAY_PORT", 8080);
        thread_num_ = config.getInt("GATEWAY_THREAD_NUM", 4);
        storage_ports_ = config.getIntList("STORAGE_PORTS");
        max_retries_ = config.getInt("RPC_MAX_RETRIES", 3);
        
        initRpcClient();
        registerRoutes();

        server_.registerHttpService(&service_);
        server_.setPort(port_);
        server_.setThreadNum(thread_num_);
        LOG_INFO(getGatewayLogger(), "Gateway running on port {}", port_);
    }
    
    ~GatewayServer() = default;
    
    void start() {
        server_.run();
    }
    
    // 获取一致性哈希实例
    ConsistentHash& getConsistentHash() {
        return consistentHash_;
    }
    
private:
    void initRpcClient() {
        // 初始化RPC客户端
        client_ = &client::get();
        client_->run();
        
        // 连接到多个存储节点
        for (int port : storage_ports_) {
            int retry_count = 0;
            bool connected = false;
            
            while (retry_count < max_retries_ && !connected) {
                auto conn = client_->connect("127.0.0.1", port);
                if (conn) {
                    storage_conns_.push_back(conn);
                    // 将节点添加到一致性哈希环
                    std::string nodeId = "node_" + std::to_string(port);
                    consistentHash_.addNode(nodeId, port, conn);
                    LOG_INFO(getGatewayLogger(), "Connected to storage node on port {}", port);
                    connected = true;
                } else {
                    retry_count++;
                    if (retry_count < max_retries_) {
                        LOG_WARN(getGatewayLogger(), "Retry {} connecting to storage node on port {}", retry_count, port);
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                }
            }
            
            if (!connected) {
                LOG_ERROR(getGatewayLogger(), "Failed to connect to storage node on port {} after {} attempts", port, max_retries_);
            }
        }
        
        if (storage_conns_.empty()) {
            LOG_ERROR(getGatewayLogger(), "No storage nodes available!");
        } else {
            LOG_INFO(getGatewayLogger(), "Successfully connected to {} storage node(s)", storage_conns_.size());
        }
    }
    
    void registerRoutes() {
        // 注册存储路由，传入一致性哈希实例和KV服务
        storage_router_ = std::make_unique<StorageRouter>(service_, consistentHash_, kvService_);
        storage_router_->RegisterRoute();
    }

    int port_{0};
    int thread_num_{0};
    int max_retries_{3};
    std::vector<int> storage_ports_;
    hv::HttpService service_;
    hv::HttpServer server_;
    std::shared_ptr<KvService> kvService_;
    
    // RPC客户端和连接列表
    client* client_;
    std::vector<std::shared_ptr<connection>> storage_conns_;
    
    // 一致性哈希
    ConsistentHash consistentHash_;
    
    // 存储路由
    std::unique_ptr<StorageRouter> storage_router_;
};

#endif // GATEWAY_HPP