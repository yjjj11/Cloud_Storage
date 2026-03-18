#include <iostream>
#include "gateway.hpp"
#include "kv_store.hpp"
#include "raftnode.hpp"
using namespace mrpc;
int main(int argc, char* argv[]) {
    std::string configFile = "config/gateway_config.env";

    try {
        auto g_raftNode = initialize_server(argc, argv);
        auto g_kvService = std::make_shared<KvService>(g_raftNode);
        std::cout << "Raft node initialized successfully" << std::endl;
        GatewayServer gateway(configFile, g_kvService);
        gateway.start();
    } catch (const std::exception& e) {
        std::cout << "Failed to initialize Raft node: " << e.what() << std::endl;
    }
    return 0;
}