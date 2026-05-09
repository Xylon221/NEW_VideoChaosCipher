#include "encryptor.h"

// Logistic map 混沌映射函数
float logisticMap(float x, float r) {
    return r * x * (1 - x);
}

// 使用混沌序列加密图片（通过异或操作，原地修改）
// seed: 混沌映射的初始种子，同一 seed 加密/解密结果一致
void encryptFrame(cv::Mat &frame, float seed) {
    const float r = 3.999f;       // 混沌参数，接近4时系统处于完全混沌状态
    float chaosValue = seed;      // 混沌序列的初始值（由种子决定）

    // 遍历每个像素，边生成混沌值边加密，不需要额外内存
    for (int i = 0; i < frame.rows; ++i) {
        for (int j = 0; j < frame.cols; ++j) {
            // 迭代 logistic 映射生成下一个混沌值
            chaosValue = logisticMap(chaosValue, r);
            // 将混沌值映射到 [0, 255] 作为加密密钥字节
            uchar key = static_cast<uchar>(chaosValue * 256);
            // 获取当前像素引用
            cv::Vec3b &pixel = frame.at<cv::Vec3b>(i, j);
            // 对 R, G, B 三个通道分别异或加密
            pixel[0] ^= key;
            pixel[1] ^= key;
            pixel[2] ^= key;
        }
    }
}
