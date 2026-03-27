#include "include/service/embedding_client.hpp"
#include <iostream>
using namespace std;

int main() {
    try {
        RAG rag;

        string long_text = R"(
            人工智能是一门旨在使计算机系统模拟人类智能的技术。它涵盖了机器学习、深度学习、自然语言处理、计算机视觉等多个领域。
            机器学习让系统能够从数据中自动学习规律并改进性能，不需要人工显式编程。深度学习则基于神经网络模型，能够处理复杂的模式识别任务。
            在自然语言处理中，机器可以理解、翻译和生成人类语言，实现智能对话、文本摘要和情感分析。
            计算机视觉技术让机器能够看懂图像和视频，应用于人脸识别、自动驾驶、医疗影像分析等场景。
            随着算力的提升和大数据的普及，人工智能正在快速发展，不断改变着人们的生活、工作和社会结构。
            未来，人工智能将在教育、医疗、工业、交通、金融等领域发挥更加重要的作用，推动社会全面智能化升级。
        )";

        rag.load_document(long_text, "AI介绍");

        cout << "=== 1.向量搜索 ===" << endl;
        auto r1 = rag.search("深度学习是什么", 2);
        for (auto& p : r1) cout << p.second << " | " << p.first << endl;

        cout << "\n=== 2.关键字搜索 ===" << endl;
        auto r2 = rag.keyword_search("深度学习", 2);
        for (auto& p : r2) cout << p.second << " | " << p.first << endl;

        cout << "\n=== 3.混合搜索 ===" << endl;
        auto r3 = rag.hybrid_search("深度学习是什么", 2, 0.7f);
        for (auto& p : r3) cout << p.second << " | " << p.first << endl;

    } catch (exception& e) {
        cerr << "错误：" << e.what() << endl;
        return 1;
    }
    return 0;
}