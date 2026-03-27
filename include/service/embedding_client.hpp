#ifndef EMBEDDING_CLIENT_HPP
#define EMBEDDING_CLIENT_HPP

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <utility>
#include <map>
#include <fstream>
#include "annoylib.h"
#include "kissrandom.h"

using json = nlohmann::json;
using namespace Annoy;

const int EMBEDDING_DIM = 1536;

// ==============================
// 单例 EmbeddingClient
// ==============================
class EmbeddingClient {
private:
    const std::string API_KEY = "sk-d5011e9c33c64b46b2b9cb83f2856e20";
    const std::string MODEL_NAME = "text-embedding-v2";

    EmbeddingClient() = default;

public:
    EmbeddingClient(const EmbeddingClient&) = delete;
    EmbeddingClient& operator=(const EmbeddingClient&) = delete;

    static EmbeddingClient& instance() {
        static EmbeddingClient client;
        return client;
    }

    std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& input_texts) {
        httplib::Client cli("https://dashscope.aliyuncs.com");
        cli.set_read_timeout(30, 0);
        cli.set_write_timeout(30, 0);

        httplib::Headers headers;
        headers.insert({"Authorization", "Bearer " + API_KEY});

        json req_body_json;
        req_body_json["model"] = MODEL_NAME;
        req_body_json["input"]["texts"] = input_texts;
        req_body_json["parameters"]["text_type"] = "document";

        std::string req_body = req_body_json.dump();

        auto res = cli.Post(
            "/api/v1/services/embeddings/text-embedding/text-embedding",
            headers,
            req_body,
            "application/json"
        );

        if (!res) throw std::runtime_error("网络错误: " + httplib::to_string(res.error()));
        if (res->status != 200) throw std::runtime_error("API错误: " + res->body);

        json resp = json::parse(res->body);
        auto& embeddings = resp["output"]["embeddings"];

        std::vector<std::vector<float>> vecs;
        for (auto& item : embeddings) {
            vecs.push_back(item["embedding"].get<std::vector<float>>());
        }
        return vecs;
    }

    std::vector<float> embed_single(const std::string& text) {
        auto vecs = embed_batch({text});
        return vecs.empty() ? std::vector<float>() : vecs[0];
    }
};

// ==============================
// RAG 类（支持文本名 + 双加载方式）
// ==============================
class RAG {
private:
    AnnoyIndex<int, float, Euclidean, Kiss32Random, AnnoyIndexSingleThreadedBuildPolicy> ann_index_;
    std::map<int, std::string> id_to_chunk_;
    std::vector<std::string> chunks_;
    std::string document_name_; // 🔥 内部保存文本名
    int next_id_ = 0; 


    std::unordered_map<std::string, std::vector<int>> inverted_index_; // 词 -> [id]
    int total_doc_num_ = 0;
    int avg_doc_len_ = 0;

    std::string read_file_content(const std::string& file_path) {
        std::ifstream file(file_path);
        if (!file.is_open()) {
            throw std::runtime_error("无法打开文件: " + file_path);
        }
        return std::string((std::istreambuf_iterator<char>(file)), 
                           std::istreambuf_iterator<char>());
    }

    // 提取文件名（从路径中）
    std::string get_filename_from_path(const std::string& path) {
        size_t pos = path.find_last_of("/\\");
        if (pos == std::string::npos) return path;
        return path.substr(pos + 1);
    }

public:
    RAG() : ann_index_(EMBEDDING_DIM) {}

    // ------------------------------
    // 1. 手动传入文本 + 必须传文本名
    // ------------------------------
    void load_document(const std::string& document_text, 
                       const std::string& document_name,  // 🔥 必传
                       int chunk_size = 500) {
                        std::string clean_content = clean_utf8(document_text); 
        document_name_ = document_name; // 保存名称
        chunks_ = split_text(clean_content, chunk_size);
        auto& client = EmbeddingClient::instance();
        auto embeddings = client.embed_batch(chunks_);

        for (size_t i = 0; i < embeddings.size(); ++i) {
            ann_index_.add_item(next_id_, embeddings[i].data());
            id_to_chunk_[next_id_] = chunks_[i];
            next_id_++;
        }
        ann_index_.build(10);
        build_inverted_index();
    }

    // ------------------------------
    // 2. 文件路径导入（自动提取文件名）
    // ------------------------------
    void load_document_from_file(const std::string& file_path, int chunk_size = 500) {
        std::string content = read_file_content(file_path);
        std::string name = get_filename_from_path(file_path);
        load_document(content, name, chunk_size);
    }

    std::string clean_utf8(const std::string& input) {
    std::string output;
    output.reserve(input.size());

    for (size_t i = 0; i < input.size();) {
        uint8_t c = static_cast<uint8_t>(input[i]);
        if (c < 0x80) {
            output.push_back(c);
            i++;
        } else if (c < 0xC0) {
            // 非法单字节，跳过
            i++;
        } else if (c < 0xE0) {
            if (i + 1 < input.size() && (static_cast<uint8_t>(input[i+1]) & 0xC0) == 0x80) {
                output.append(input, i, 2);
            }
            i += 2;
        } else if (c < 0xF0) {
            if (i + 2 < input.size() && 
                (static_cast<uint8_t>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<uint8_t>(input[i+2]) & 0xC0) == 0x80) {
                output.append(input, i, 3);
            }
            i += 3;
        } else if (c < 0xF8) {
            if (i + 3 < input.size() && 
                (static_cast<uint8_t>(input[i+1]) & 0xC0) == 0x80 &&
                (static_cast<uint8_t>(input[i+2]) & 0xC0) == 0x80 &&
                (static_cast<uint8_t>(input[i+3]) & 0xC0) == 0x80) {
                output.append(input, i, 4);
            }
            i += 4;
        } else {
            i++;
        }
    }
    return output;
}
    // ------------------------------
    // 文本切片
    // ------------------------------
    std::vector<std::string> split_text(const std::string& text, int max_chunk_size = 30) {
        std::vector<std::string> chunks;
        if (text.empty()) return chunks;

        // 句子结束符（语义断点）
        const std::string delimiters = "。！？；.!?;\n";
        size_t start = 0;
        size_t length = text.size();

        while (start < length) {
            size_t end = start + max_chunk_size;
            if (end >= length) {
                chunks.push_back(text.substr(start));
                break;
            }

            // 从 end 往前找最近的句子断点
            size_t split_pos = end;
            while (split_pos > start) {
                if (delimiters.find(text[split_pos]) != std::string::npos) {
                    break;
                }
                split_pos--;
            }

            // 如果找不到断点，或者断点离 start 还不到 20，就强制在 max_chunk_size 切分
            if (split_pos == start || (split_pos - start) < 20) {
                split_pos = end;
            }

            std::string chunk = text.substr(start, split_pos - start + 1);
            // 去掉前后空白（让分片更干净）
            size_t first = chunk.find_first_not_of(" \t\n\r");
            size_t last = chunk.find_last_not_of(" \t\n\r");
            if (first != std::string::npos) {
                chunk = chunk.substr(first, last - first + 1);
            }
            if (!chunk.empty()) {
                chunks.push_back(chunk);
            }
            start = split_pos + 1;
        }

        return chunks;
    }

    // ------------------------------
    // 搜索
    // ------------------------------
    std::vector<std::pair<std::string, float>> search(const std::string& query, int top_k = 3) {
        auto& client = EmbeddingClient::instance();
        auto query_vec = client.embed_single(query);

        std::vector<int> ids;
        std::vector<float> dists;
        ann_index_.get_nns_by_vector(query_vec.data(), top_k, -1, &ids, &dists);

        std::vector<std::pair<std::string, float>> results;
        const float lambda = 0.5f; // 缩放因子，可根据需要调整
        for (size_t i = 0; i < ids.size(); ++i) {
            float distance = dists[i];
            float score = exp(-lambda * distance); // 指数归一化 → 0~1
            results.emplace_back(id_to_chunk_[ids[i]], score);
        }
        return results;
    }

    // ------------------------------
    // 获取文本名（你可以随时调用）
    // ------------------------------
    std::string get_document_name() const {
        return document_name_;
    }

    std::vector<std::string> get_chunks() const { return chunks_; }

    std::vector<std::string> tokenize(const std::string& text) {
        std::vector<std::string> tokens;
        for (char c : text) {
            if (std::isalnum(c) || (c >= 0x4E00 && c <= 0x9FA5)) {
                tokens.push_back(std::string(1, c));
            }
        }
        return tokens;
    }

    // ==============================
    // 构建倒排索引
    // ==============================
    void build_inverted_index() {
        total_doc_num_ = chunks_.size();
        int total_len = 0;

        for (int i = 0; i < chunks_.size(); ++i) {
            auto tokens = tokenize(chunks_[i]);
            total_len += tokens.size();

            for (auto& t : tokens) {
                inverted_index_[t].push_back(i);
            }
        }

        if (total_doc_num_ > 0)
            avg_doc_len_ = total_len / total_doc_num_;
    }

    // ==============================
    // BM25 关键字搜索（精确匹配）
    // ==============================
    std::vector<std::pair<std::string, float>> keyword_search(const std::string& query, int top_k = 3) {
        auto query_tokens = tokenize(query);
        std::map<int, float> scores;

        const float k1 = 1.5f;
        const float b = 0.75f;

        for (auto& t : query_tokens) {
            if (!inverted_index_.count(t)) continue;

            auto& doc_ids = inverted_index_[t];
            float idf = log((total_doc_num_ - doc_ids.size() + 0.5f) / (doc_ids.size() + 0.5f) + 1.0f);

            for (int id : doc_ids) {
                int doc_len = tokenize(chunks_[id]).size();
                float tf = (std::count(doc_ids.begin(), doc_ids.end(), id));
                float numerator = tf * (k1 + 1.0f);
                float denominator = tf + k1 * (1.0f - b + b * doc_len / avg_doc_len_);
                scores[id] += idf * numerator / denominator;
            }
        }

        std::vector<std::pair<int, float>> sorted_scores(scores.begin(), scores.end());
        std::sort(sorted_scores.begin(), sorted_scores.end(), [](auto& a, auto& b) {
            return a.second > b.second;
        });

        std::vector<std::pair<std::string, float>> res;
        int limit = std::min(top_k, (int)sorted_scores.size());
        for (int i = 0; i < limit; ++i) {
            int id = sorted_scores[i].first;
            float score = sorted_scores[i].second;
            res.emplace_back(chunks_[id], score);
        }

        return res;
    }

    std::vector<std::pair<std::string, float>> hybrid_search(
        const std::string& question,
        int top_k = 3,
        float alpha = 0.7f
    ) {
        auto v_res = search(question, 20);
        auto k_res = keyword_search(question, 20);

        std::unordered_map<std::string, float> v_map, k_map;
        for (auto& p : v_res) v_map[p.first] = p.second;
        for (auto& p : k_res) k_map[p.first] = p.second;

        std::unordered_map<std::string, float> final_map;
        for (auto& p : v_res) final_map[p.first] = alpha * p.second;
        for (auto& p : k_res) final_map[p.first] += (1 - alpha) * p.second;

        std::vector<std::pair<std::string, float>> final(final_map.begin(), final_map.end());
        std::sort(final.begin(), final.end(), [](auto& a, auto& b) { return a.second > b.second; });

        int limit = std::min(top_k, (int)final.size());
        return std::vector<std::pair<std::string, float>>(final.begin(), final.begin() + limit);
    }
};

#endif