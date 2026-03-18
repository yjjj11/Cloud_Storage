#pragma once
#include <sys/stat.h>
#include <sys/types.h>
#include <nlohmann/json.hpp>

#include <ctime>
#include <fstream>
#include <iostream>

namespace mylog {
namespace Util {

class Date {
public:
    static time_t Now() { return time(nullptr); }
};

class File {
public:
    static bool Exists(const std::string &filename) {
        struct stat st;
        return (0 == stat(filename.c_str(), &st));
    }

    static std::string Path(const std::string &filename) {
        if (filename.empty())
            return "";
        int pos = filename.find_last_of("/\\");
        if (pos != std::string::npos)
            return filename.substr(0, pos + 1);
        return "";
    }

    static void CreateDirectory(const std::string &pathname) {
        if (pathname.empty()) {
            perror("文件所给路径为空：");
            return;
        }

        if (!Exists(pathname)) {
            size_t pos, index = 0;
            size_t size = pathname.size();
            while (index < size) {
                pos = pathname.find_first_of("/\\", index);
                if (pos == std::string::npos) {
                    mkdir(pathname.c_str(), 0755);
                    return;
                }
                if (pos == index) {
                    index = pos + 1;
                    continue;
                }

                std::string sub_path = pathname.substr(0, pos);
                if (sub_path == "." || sub_path == "..") {
                    index = pos + 1;
                    continue;
                }
                if (Exists(sub_path)) {
                    index = pos + 1;
                    continue;
                }

                mkdir(sub_path.c_str(), 0755);
                index = pos + 1;
            }
        }
    }

    int64_t FileSize(std::string filename) {
        struct stat s;
        auto ret = stat(filename.c_str(), &s);
        if (ret == -1) {
            perror("Get file size failed");
            return -1;
        }
        return s.st_size;
    }

    bool GetContent(std::string *content, std::string filename) {
        std::ifstream ifs;
        ifs.open(filename.c_str(), std::ios::binary);
        if (!ifs.is_open()) {
            std::cout << "file open error" << std::endl;
            return false;
        }

        ifs.seekg(0, std::ios::beg);
        size_t len = FileSize(filename);
        content->resize(len);
        ifs.read(&(*content)[0], len);
        if (!ifs.good()) {
            std::cout << __FILE__ << __LINE__ << "-" << "read file content error" << std::endl;
            ifs.close();
            return false;
        }
        ifs.close();

        return true;
    }
};

struct JsonData {
    using json = nlohmann::json;

    static JsonData* GetJsonData() {
        static JsonData* json_data = new JsonData;
        return json_data;
    }

private:
    JsonData() {
        std::string content;
        File file;
        if (!file.GetContent(&content, "../config/config.conf")) {
            std::cout << __FILE__ << __LINE__ << "open config.conf failed" << std::endl;
            perror(nullptr);
        }

        try {
            json root = json::parse(content);
            buffer_size = root["buffer_size"].get<size_t>();
            threshold = root["threshold"].get<size_t>();
            linear_growth = root["linear_growth"].get<size_t>();
            flush_log = root["flush_log"].get<size_t>();
            backup_addr = root["backup_addr"].get<std::string>();
            backup_port = root["backup_port"].get<uint16_t>();
            thread_count = root["thread_count"].get<size_t>();
        } catch (const std::exception& e) {
            std::cout << __FILE__ << __LINE__ << "parse config.conf failed: " << e.what() << std::endl;
        }
    }

public:
    size_t buffer_size;      // 缓冲区基础容量
    size_t threshold;        // 倍数扩容阈值
    size_t linear_growth;    // 线性增长容量
    size_t flush_log;        // 控制日志同步到磁盘的时机，默认为0,1调用fflush，2调用fsync
    std::string backup_addr;
    uint16_t backup_port;
    size_t thread_count;
};

} // namespace Util
} // namespace mylog
