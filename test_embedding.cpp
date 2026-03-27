#include "include/service/embedding_client.hpp"
using namespace std;

int main() {
    try {
        std::string api_key = "sk-d5011e9c33c64b46b2b9cb83f2856e20"; // 你的key
        EmbeddingClient client(api_key);

        // ================= 测试：批量嵌入 3 句话 ================
        std::vector<std::string> texts = {
            "你好",
            "我喜欢人工智能",
            "今天天气不错"
        };

        auto embeddings = client.embed(texts);

        std::cout << "成功生成向量数量: " << embeddings.size() << "\n";
        std::cout << "每个向量维度: " << embeddings[0].size() << "\n\n";

        // 打印第一个向量前 5 个值
        std::cout << "第一个句子 [你好] 前5个值:\n";
        for (int i = 0; i < 5; ++i) {
            std::cout << embeddings[0][i] << " ";
        }
        std::cout << "\n";

    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << "\n";
        return 1;
    }

    return 0;
}