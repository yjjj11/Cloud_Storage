cmake_minimum_required(VERSION 3.14)
project(AsyncLogger)

add_compile_options(-w)
# 先查找线程库（保证 Threads::Threads 全局可见）
find_package(Threads REQUIRED)

# 引入 raftKV 子目录（进而引入 Asio_mrpc）
add_subdirectory(third_party/raftKV)

# 全局产物输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# C++20 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 生成 compile_commands.json
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_custom_target(copy_compile_commands ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/compile_commands.json
        ${CMAKE_SOURCE_DIR}/compile_commands.json
)

link_directories(/usr/local/lib)

# 包含目录：继承 raftKV 传递上来的 Asio_mrpc 路径
include_directories(
    include
    include/service
    third_party/raftKV/include  # raftKV 的头文件目录
    ${RPC_INCLUDE_DIR}
    ${ASIO_INCLUDE_DIR}
    ${SPDLOG_INCLUDE_DIR}
    ${WLOG_INCLUDE_DIR}
    ${NLOHMANN_INCLUDE_DIR}
)

# 创建必要目录
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/logs)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

# 编译 testRpc 可执行文件
add_executable(testRpc
    Test/testRpc.cpp
    ${MRPC_SRC_FILES}  # 引入 Asio_mrpc 源码
)

# 链接依赖库
target_link_libraries(testRpc 
    pthread 
    hv 
    Threads::Threads
)

add_executable(testRaft
    Test/testRaft.cpp
    ${MRPC_SRC_FILES}  # 引入 Asio_mrpc 源码
    ${RAFTKV_SRC_FILES}  # 引入 raftKV 源码
)

# 链接依赖库
target_link_libraries(testRaft  
    pthread 
    hv 
    Threads::Threads
)

# 网关源文件
add_executable(gateway
    src/log/gateway.cpp
    ${MRPC_SRC_FILES}  # 引入 Asio_mrpc 源码
    ${RAFTKV_SRC_FILES}  # 引入 raftKV 源码
)

# 链接依赖库
target_link_libraries(gateway 
    pthread 
    hv 
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
)

# 存储节点源文件（三个节点）
add_executable(storage_node
    src/log/storage_node.cpp
    ${MRPC_SRC_FILES}  # 引入 Asio_mrpc 源码
    ${RAFTKV_SRC_FILES}  # 引入 raftKV 源码
)

# 链接依赖库
target_link_libraries(storage_node
    pthread 
    hv
    Threads::Threads
)

find_package(OpenSSL REQUIRED)

add_executable(qwen_client qwen_stream_client.cpp)

# 链接 OpenSSL 库
target_link_libraries(qwen_client PRIVATE ${OPENSSL_LIBRARIES})

