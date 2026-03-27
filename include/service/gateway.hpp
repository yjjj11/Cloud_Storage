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
                // 去除首尾空格和换行符
                key.erase(0, key.find_first_not_of(" \t\r\n"));
                if (!key.empty()) {
                    key.erase(key.find_last_not_of(" \t\r\n") + 1);
                }
                value.erase(0, value.find_first_not_of(" \t\r\n"));
                if (!value.empty()) {
                    value.erase(value.find_last_not_of(" \t\r\n") + 1);
                }
                
                std::cout << "Config loaded: [" << key << "] = [" << value << "]" << std::endl;
                configs_[key] = value;
            }
        }
        file.close();
    }
    
    std::unordered_map<std::string, std::string> configs_;
};

class GatewayServer {
public:
    GatewayServer(const std::string& configFile = "config/gateway_config.env", std::shared_ptr<KvService> kvService = nullptr) : kvService_(kvService) {
        std::cout << "Loading config..." << std::endl;
        ConfigManager config(configFile);
        port_ = config.getInt("GATEWAY_PORT", 8080);
        thread_num_ = config.getInt("GATEWAY_THREAD_NUM", 4);
        storage_ports_ = config.getIntList("STORAGE_PORTS");
        max_retries_ = config.getInt("RPC_MAX_RETRIES", 3);
        
        std::cout << "Starting RPC client..." << std::endl;
        client_ = &client::get();
        client_->run(); // 启动 RPC 客户端的 io_context 线程池
        
        std::cout << "Initializing client connections..." << std::endl;
        initClient();
        
        std::cout << "Registering watches..." << std::endl;
        registerWatch();
        
        std::cout << "Registering routes..." << std::endl;
        registerRoutes();
        
        std::cout << "Configuring libhv server..." << std::endl;
        server_.registerHttpService(&service_);
        server_.setPort(port_);
        server_.setThreadNum(thread_num_);
        LOG_INFO(getGatewayLogger(), "Gateway configured to run on port {}", port_);
    }
    
    ~GatewayServer() = default;
    
    void registerWatch(){
        if (kvService_) {
            // 监听节点上线
            kvService_->WATCH("put","监听节点上线",[this](const std::string& key, const std::string& value) -> bool {
                if (key.substr(0, 8) == "NodePort:") {
                    auto port=std::stoi(value);
                    auto nodeId=key.substr(8);
                    auto conn=client::get().connect("127.0.0.1", port);
                    if (conn) {
                        consistentHash_.addNode(nodeId,port,conn);
                        std::cout << "Node " << nodeId << " with port " << port << " added to hash" << std::endl;
                    } else {
                        std::cout << "Failed to connect to node " << nodeId << " on port " << port << "" << std::endl;
                    }
                }
                return true;
            });
            
            // 监听节点下线
            kvService_->WATCH("del","监听节点下线",[this](const std::string& key, const std::string& value) -> bool {
                if (key.substr(0, 8) == "NodePort:") {
                    auto nodeId=key.substr(8);
                    auto port=std::stoi(value);
                    consistentHash_.removeNode(nodeId,port);
                    std::cout << "Node " << nodeId << " with port " << port << " removed from hash" << std::endl;
                }
                return true;
            });
        }
    }
    
    // 启动健康检查线程
    
    void initClient(){
        std::cout << "initClient: " << storage_ports_.size() << " ports found." << std::endl;
        for(size_t i=0;i<storage_ports_.size();i++){
            auto port=storage_ports_[i];
            std::cout << "initClient: trying to connect to port " << port << std::endl;
            // 使用较短的超时时间进行连接，防止长时间阻塞
            auto conn=client::get().connect("127.0.0.1", port, 100); 
            if (conn) {
                storage_conns_.push_back(conn);
                consistentHash_.addNode(std::to_string(i),port,conn);
                std::cout << "initClient: connected to port " << port << std::endl;
            } else {
                std::cout << "Failed to connect to node " << port << "" << std::endl;
            }
        }
        std::cout << "initClient done." << std::endl;
    }
   
    
    void start() {
        std::cout << "Gateway starting libhv HttpServer..." << std::endl;
        server_.run();
    }
    
    // 获取一致性哈希实例
    ConsistentHash& getConsistentHash() {
        return consistentHash_;
    }
    
private:
    void registerRoutes() {
        // 注册存储路由，传入一致性哈希实例和KV服务
        storage_router_ = std::make_unique<StorageRouter>(service_, consistentHash_);
        storage_router_->RegisterRoute();
    }

    int port_{0};
    int thread_num_{0};
    int max_retries_{3};
    std::vector<int> storage_ports_;
    hv::HttpService service_;
    hv::HttpServer server_;
    
    // RPC客户端和连接列表
    client* client_;
    std::vector<std::shared_ptr<connection>> storage_conns_;
    
    // 一致性哈希
    ConsistentHash consistentHash_;
    
    // 存储路由
    std::unique_ptr<StorageRouter> storage_router_;

    std::shared_ptr<KvService> kvService_;
    
};

#endif // GATEWAY_HPP