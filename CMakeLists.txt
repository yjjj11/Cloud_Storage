cmake_minimum_required(VERSION 3.14)
project(AsyncLogger)

add_compile_options(-w)
# 先查找线程库（解决 Threads::Threads 作用域问题）
find_package(Threads REQUIRED)

# 引入 Asio_mrpc 子项目（必须先执行，才能使用它导出的变量）
add_subdirectory(third_party/Asio_mrpc)

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

# 包含目录：继承 Asio_mrpc 导出的路径
include_directories(include)
include_directories(include/service)
include_directories(
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
    test.cpp
    ${MRPC_SRC_FILES}  # 引入 Asio_mrpc 源码
)

# 链接依赖库
target_link_libraries(testRpc pthread hv ${ZK_LIB} Threads::Threads)

# # 编译可执行文件（无需再单独配置输出目录）
# add_executable(logger 
#     src/log/main.cpp
# )

# # 链接依赖库
# target_link_libraries(logger pthread fmt hv)


# # 服务端源文件
# add_executable(log_server
#     src/log/server.cpp
# )

# # 链接依赖库
# target_link_libraries(log_server pthread hv)