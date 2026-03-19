#ifndef HASH_HPP
#define HASH_HPP

#include <random>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <memory>
#include <mrpc/client.hpp>
// 启用nlohmann/json的宏支持（需要nlohmann/json版本>=3.0）
#define NLOHMANN_JSON_NAMESPACE_NO_VERSION
using json = nlohmann::json;
using namespace mrpc;
// 节点信息结构
struct NodeInfo {
    std::string id;
    int port;
    // 默认构造函数
    NodeInfo() : id(""), port(0) {}
    // 带参数的构造函数
    NodeInfo(const std::string& id_, int port_) : id(id_), port(port_) {}

    std::shared_ptr<connection> conn;
};

// 一致性哈希实现
class ConsistentHash {
private:
    // 哈希函数
    size_t hashFunc(const std::string& key) {
        // 使用简单的哈希函数，实际生产环境中可使用更复杂的哈希函数
        size_t hash = 0;
        for (char c : key) {
            hash = hash * 31 + c;
        }
        return hash;
    }

public:
    // 添加存储节点
    void addNode(const std::string& nodeId, int port, std::shared_ptr<connection> conn) {
        // 为每个节点创建多个虚拟节点
        for (int i = 0; i < 160; i++) {
            std::string virtualNode = nodeId + "#" + std::to_string(i);
            size_t hash = hashFunc(virtualNode);
            NodeInfo nodeInfo(nodeId, port);
            nodeInfo.conn = conn;
            hashRing_[hash] = nodeInfo;
            port_to_node_id_[std::to_string(port)] = nodeInfo;
        }
        // 排序哈希环
        sortedHashes_.clear();
        for (const auto& pair : hashRing_) {
            sortedHashes_.push_back(pair.first);
        }
        std::sort(sortedHashes_.begin(), sortedHashes_.end());
    }

    // 查找负责该key的节点
    NodeInfo getResponsibleNode(const std::string& key) {
        if (hashRing_.empty()) {
            throw std::runtime_error("No nodes in hash ring");
        }

        size_t hash = hashFunc(key);
        // 找到第一个大于等于hash的节点
        auto it = std::lower_bound(sortedHashes_.begin(), sortedHashes_.end(), hash);
        if (it == sortedHashes_.end()) {
            it = sortedHashes_.begin();
        }
        return hashRing_[*it];
    }

    // 移除节点
    void removeNode(const std::string& nodeId, int port) {
        // 移除该节点的所有虚拟节点
        for (int i = 0; i < 160; i++) {
            std::string virtualNode = nodeId + "#" + std::to_string(i);
            size_t hash = hashFunc(virtualNode);
            hashRing_.erase(hash);
        }
        // 移除端口到节点的映射
        port_to_node_id_.erase(std::to_string(port));
        // 重新排序哈希环
        sortedHashes_.clear();
        for (const auto& pair : hashRing_) {
            sortedHashes_.push_back(pair.first);
        }
        std::sort(sortedHashes_.begin(), sortedHashes_.end());
    }

    NodeInfo getConnbyNodeport(const std::string& nodeport) {
        return port_to_node_id_[nodeport];
    }
public:
    std::unordered_map<size_t, NodeInfo> hashRing_;
    std::unordered_map<std::string, NodeInfo> port_to_node_id_;
    
private:
    std::vector<size_t> sortedHashes_;
};

// 生成唯一的fileId
std::string generateFileId(const std::string& filename) {
    // 生成基于时间戳和随机数的唯一ID
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    // 生成随机数
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    uint64_t random = dis(gen);
    
    // 组合生成fileId
    std::stringstream ss;
    ss << timestamp << "_" << random << "_" << filename;
    return ss.str();
}

// 云存储元数据结构体（带默认值）
struct CloudStorageMetadata {
    // 核心字段
    std::string fileId;          // 文件/对象唯一标识
    std::string filename;        // 原始文件名
    uint64_t size;               // 文件字节大小
    std::string contentType;     // 文件MIME类型
    std::string etag;            // 文件哈希值
    int64_t createTime;          // 创建时间戳（UTC，秒）
    int64_t updateTime;          // 最后修改时间戳
    std::string storageClass;    // 存储类型
    std::string bucketName;      // 所属存储桶名称
    std::string path;            // 文件在桶中的路径

    // 带默认值的构造函数（核心修改）
    CloudStorageMetadata() 
        : fileId("")
        , filename("")
        , size(0)  // 默认文件大小为0
        , contentType("application/octet-stream")  // 默认二进制流（通用未知类型）
        , etag("")
        , createTime(std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
          ).count())  // 默认当前UTC时间戳（秒）
        , updateTime(createTime)  // 默认和创建时间一致
        , storageClass("STANDARD")  // 默认标准存储
        , bucketName("default-bucket")  // 默认桶名称
        , path("/")  // 默认根路径
    {}

    // 序列化/反序列化宏（nlohmann/json核心）
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        CloudStorageMetadata,
        fileId,
        filename,
        size,
        contentType,
        etag,
        createTime,
        updateTime,
        storageClass,
        bucketName,
        path
    )
};

#endif // HASH_HPP