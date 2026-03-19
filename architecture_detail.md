# 云存储系统架构详细文档

## 1. 项目结构概览

```
/home/yjj/log_and_storage/
├── include/
│   ├── service/
│   │   ├── hash.hpp              # 一致性哈希和元数据结构
│   │   ├── storage_router.hpp    # HTTP路由处理（上传/下载/删除/列举）
│   │   ├── gateway.hpp           # 网关服务器主类
│   │   └── route.hpp             # 路由基类
│   ├── log/
│   │   └── AsyncLogger.hpp       # 异步日志系统
│   └── resource/
│       └── index.html            # 前端页面
├── src/
│   └── log/
│       ├── gateway.cpp           # 网关入口
│       └── storage_node.cpp      # 存储节点入口
├── third_party/
│   └── raftKV/                   # Raft分布式KV存储
│       ├── include/
│       │   ├── kv_store.hpp      # KV服务接口
│       │   └── raftnode.hpp      # Raft节点实现
│       └── Asio_mrpc/            # RPC框架
├── config/
│   └── gateway_config.env        # 网关配置文件
└── architecture_detail.md        # 本文档
```

## 2. 核心组件详解

### 2.1 一致性哈希 (hash.hpp)

**NodeInfo 结构体**
```cpp
struct NodeInfo {
    std::string id;                    // 节点ID，如 "node_8001"
    int port;                          // 节点端口
    std::shared_ptr<connection> conn;  // RPC连接
};
```

**ConsistentHash 类**
- `hashRing_`: 哈希环，key是哈希值，value是NodeInfo
- `port_to_node_id_`: 端口到节点的映射
- `sortedHashes_`: 排序后的哈希值列表

**关键方法**
```cpp
// 添加节点（创建160个虚拟节点）
void addNode(const std::string& nodeId, int port, std::shared_ptr<connection> conn)

// 根据key获取负责节点
NodeInfo getResponsibleNode(const std::string& key)

// 根据端口获取节点
NodeInfo getConnbyNodeId(const std::string& nodeport)
```

**generateFileId 函数** ⚠️ 重要
```cpp
std::string generateFileId(const std::string& filename) {
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis(0, UINT64_MAX);
    uint64_t random = dis(gen);
    
    std::stringstream ss;
    ss << timestamp << "_" << random << "_" << filename;
    return ss.str();
}
```
**注意：每次调用generateFileId都会产生不同的结果，因为包含时间戳和随机数**

### 2.2 云存储元数据 (CloudStorageMetadata)

```cpp
struct CloudStorageMetadata {
    std::string fileId;          // 文件唯一标识（由generateFileId生成）
    std::string filename;        // 原始文件名
    uint64_t size;               // 文件大小
    std::string contentType;     // MIME类型
    std::string etag;            // 文件哈希
    int64_t createTime;          // 创建时间戳
    int64_t updateTime;          // 更新时间戳
    std::string storageClass;    // 存储类型（如"STANDARD"）
    std::string bucketName;      // 存储桶名称（当前存储节点端口）
    std::string path;            // 文件路径
};
```

### 2.3 存储路由 (storage_router.hpp)

**HTTP接口**

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | /health | 健康检查 |
| GET | / | 返回前端页面 |
| POST | /api/storage/upload | 文件上传 |
| GET | /api/storage/download/{filename} | 文件下载 |
| GET | /api/storage/list | 列举文件 |
| DELETE | /api/storage/delete/{filename} | 删除文件 |

**上传流程**
1. 从HTTP请求获取filename和file_content
2. 调用`generateFileId(filename)`生成fileId ⚠️ 注意：这里生成的fileId是唯一的
3. 使用`consistentHash_.getResponsibleNode(fileId)`选择存储节点
4. 填充CloudStorageMetadata结构体
5. RPC调用存储节点的`upload_file(metadata, file_content)`
6. 存储节点使用`fileId + "_" + filename`作为本地文件名存储

**下载流程**
1. 从URL获取filename
2. 从RaftKV查询元数据：`kvService_->Get("metadata:" + filename)` ⚠️ 问题：这里用的是filename而不是fileId
3. 解析元数据获取bucketName（存储节点端口）
4. 使用`consistentHash_.getConnbyNodeId(metadata.bucketName)`获取节点
5. RPC调用存储节点的`download_file(filename)` ⚠️ 问题：这里用的是filename而不是fileId

**删除流程**
1. 从URL获取filename
2. 调用`generateFileId(filename)`生成fileId ⚠️ 问题：这里会生成新的fileId，与上传时的不同
3. 从RaftKV查询元数据：`kvService_->Get("metadata:" + fileId)`
4. 使用`consistentHash_.getResponsibleNode(fileId)`选择节点
5. RPC调用存储节点的`delete_file(fileId)`

**列举流程**
1. 遍历`consistentHash_.port_to_node_id_`中的所有节点
2. 对每个节点RPC调用`list_files(0)`
3. 合并所有节点的文件列表返回

### 2.4 存储节点 (storage_node.cpp)

**RPC服务方法**

```cpp
// 上传文件
std::string upload_file(const CloudStorageMetadata &metadata, const std::string &content)
// 存储路径: STORAGE_DIR + "/" + metadata.fileId + "_" + metadata.filename
// 同时将元数据存入RaftKV: key="metadata:" + metadata.fileId

// 下载文件
std::string download_file(const std::string &filename)
// 读取路径: STORAGE_DIR + "/" + filename

// 列举文件
std::string list_files(int dummy)
// 遍历STORAGE_DIR目录，返回文件列表

// 删除文件
std::string delete_file(const std::string &filename)
// 删除路径: STORAGE_DIR + "/" + filename
// 同时从RaftKV删除元数据: key="metadata:" + filename
```

### 2.5 RaftKV 服务 (kv_store.hpp)

**KvService 类**

```cpp
// 写入操作（异步，返回request_id）
int64_t Put(const std::string& key, const std::string& value)
int64_t PutWithTTL(const std::string& key, const std::string& value, long long ttl_ms)
int64_t Del(const std::string& key)
int64_t Cas(const std::string& key, const std::string& expected_value, const std::string& new_value)

// 读取操作（同步，通过Raft保证线性一致性）
std::string Get(const std::string& key)
bool Exists(const std::string& key)

// 等待操作完成
bool get_reply_by_id(int64_t req_id)
bool wait_for(int64_t req_id)
```

**键值设计**
- 元数据存储: `metadata:{fileId}` -> JSON序列化的CloudStorageMetadata

### 2.6 网关服务器 (gateway.hpp)

**GatewayServer 类**

```cpp
// 构造函数流程
1. 读取配置文件(config/gateway_config.env)
2. 初始化RPC客户端
3. 连接到所有存储节点
4. 将节点添加到一致性哈希环
5. 注册HTTP路由
6. 启动HTTP服务器

// 配置项
GATEWAY_PORT=8080           // 网关端口
GATEWAY_THREAD_NUM=4        // 工作线程数
STORAGE_PORTS=8001,8002     // 存储节点端口列表
RPC_MAX_RETRIES=3           // RPC重试次数
```

## 3. 关键问题分析

### 3.1 fileId生成问题 ⚠️ 严重

`generateFileId()`每次调用都会产生不同的结果，因为包含时间戳和随机数。

**影响：**
- 上传时生成的fileId与下载/删除时生成的fileId不同
- 导致无法通过filename找到正确的文件

**当前存储结构：**
- RaftKV中元数据的key: `metadata:{fileId}`（上传时的fileId）
- 本地文件名: `{fileId}_{filename}`（上传时的fileId）

**问题场景：**
1. 用户上传文件"test.txt"，生成fileId="123456_xxx_test.txt"
2. 元数据存储在`metadata:123456_xxx_test.txt`
3. 文件存储为`123456_xxx_test.txt_test.txt`
4. 用户下载时，generateFileId生成新的fileId="789012_yyy_test.txt"
5. 查询`metadata:789012_yyy_test.txt`找不到数据

### 3.2 下载接口问题

当前下载接口从RaftKV查询时使用的是filename而不是fileId：
```cpp
auto metadata_str = kvService_->Get("metadata:" + filename);
```
但存储时使用的是fileId：
```cpp
std::string key = "metadata:" + metadata.fileId;
```

### 3.3 删除接口问题

删除接口调用generateFileId生成新的fileId，与上传时的fileId不同。

## 4. 解决方案建议

### 方案1：filename到fileId的映射表

在RaftKV中维护一个反向映射：
```
filename:{filename} -> {fileId}
```

上传时：
```cpp
kvService_->Put("filename:" + filename, fileId);
kvService_->Put("metadata:" + fileId, metadata_json);
```

下载/删除时：
```cpp
std::string fileId = kvService_->Get("filename:" + filename);
std::string metadata = kvService_->Get("metadata:" + fileId);
```

### 方案2：使用确定性fileId

修改generateFileId，使用文件名哈希而不是随机数：
```cpp
std::string generateFileId(const std::string& filename) {
    std::hash<std::string> hasher;
    size_t hash = hasher(filename);
    return std::to_string(hash) + "_" + filename;
}
```

### 方案3：使用filename作为key

直接使用filename作为RaftKV的key，不依赖fileId查询元数据。

## 5. 接口调用示例

### 5.1 上传文件
```http
POST /api/storage/upload?filename=test.txt
Content-Type: application/octet-stream

<文件内容>
```

### 5.2 下载文件
```http
GET /api/storage/download/test.txt
```

### 5.3 删除文件
```http
DELETE /api/storage/delete/test.txt
```

### 5.4 列举文件
```http
GET /api/storage/list
```

## 6. 启动流程

### 6.1 启动存储节点
```bash
./bin/storage_node 0 127.0.0.1 8001 1 127.0.0.1:8080
./bin/storage_node 0 127.0.0.1 8002 1 127.0.0.1:8080
```

### 6.2 启动网关
```bash
./bin/gateway 0 127.0.0.1 5001 1 127.0.0.1:8080
```

## 7. 依赖关系

- **mrpc**: RPC通信框架
- **raftKV**: 分布式一致性KV存储
- **hv (libhv)**: HTTP服务器库
- **nlohmann/json**: JSON序列化库
- **spdlog**: 日志库

## 8. 注意事项

1. **fileId的唯一性**: 每次调用generateFileId都会产生不同的值
2. **RaftKV的Get是同步的**: 通过提交barrier日志保证线性一致性
3. **存储节点使用独立目录**: 每个节点使用`storage_{port}`目录
4. **虚拟节点数量**: 一致性哈希中每个物理节点创建160个虚拟节点
5. **RPC连接复用**: NodeInfo中缓存了connection对象，避免重复创建
