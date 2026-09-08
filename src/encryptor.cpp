#include "encryptor.h"

// ============================================================
// encryptor.cpp — Logistic Map 混沌加密实现
// ============================================================
// 这是整个项目最核心、最简单的文件，建议从这里开始阅读。
//
// 看懂这一行就能理解整个加密方案:
//   x_{n+1} = 3.999 * x_n * (1 - x_n)   →   混沌序列
//   cipher_byte[i] = chaos_value[i] * 256   →   密钥字节
//   encrypted_pixel ^= cipher_byte            →   XOR 加密
//
// 为什么 XOR 加密/解密用同一个函数?
//   异或的自逆性: (A ^ K) ^ K = A
//   所以 encryptFrame(frame, 0.5) 加密，再 encryptFrame(frame, 0.5) 解密，
//   两帧完全一致 (需要在同一起始帧位置调用)。
//
// 为什么 Logistic Map 适合加密?
//   1. 确定性: 相同 x_0 → 相同序列 → 可解密
//   2. 初值敏感: x_0 = 0.5000001 vs 0.5000000 → 完全不同的序列
//      → 只知道 ciphertext + 算法不能解，必须知道精确 seed
//   3. 计算极快: 每个像素只需 1 次乘法和 1 次减法
//
// 为什么不直接用 rand()?
//   rand() 的种子空间只有 2^32，暴力枚举可行;
//   float seed 有 ~2^23 种有效值，且 Logistic Map 的初值敏感性
//   等效于更大的密钥空间。
// ============================================================

// Logistic map 混沌映射函数
// 数学公式: f(x) = r * x * (1 - x)
// 这是最简单的混沌动力系统——抛物映射 (也叫 logistic 差分方程)
// 当 r ∈ (3.57, 4] 时系统进入混沌态
// 我们用 r = 3.999 接近上界，混沌性最强
float logisticMap(float x, float r) {
    return r * x * (1 - x);
}

// 使用混沌序列加密图片 (原地修改)
// 加密方式: 流密码，逐像素 XOR
// seed: 混沌映射的初始种子，范围 (0, 1)
//       同一 seed 加密/解密两次 = 恢复原图
void encryptFrame(cv::Mat &frame, float seed) {
    const float r = 3.999f;       // 混沌参数: 越大越混沌，取接近 4 的值
    float chaosValue = seed;      // 混沌序列的当前值，从种子出发迭代

    // 遍历每个像素，边迭代混沌映射边加密
    // 空间复杂度 O(1): 不需要预先生成整个混沌序列，只需要当前值
    // 时间复杂度 O(rows * cols): 每个像素一次迭代
    for (int i = 0; i < frame.rows; ++i) {
        for (int j = 0; j < frame.cols; ++j) {
            // 迭代 logistic 映射得到下一个混沌状态
            chaosValue = logisticMap(chaosValue, r);

            // 将混沌值 (0, 1) 映射到 [0, 255] 作为密钥字节
            // chaosValue 永远不会是精确的 0 或 1 (不动点)，所以 key ∈ [0, 255]
            uchar key = static_cast<uchar>(chaosValue * 256);

            // 获取当前像素引用 (cv::Vec3b = 3 个 uchar: B, G, R)
            cv::Vec3b &pixel = frame.at<cv::Vec3b>(i, j);

            // 对 B, G, R 三个通道分别 XOR 同一个 key
            // 为什么三个通道用同一个 key?
            //   安全上: 每像素 3 个独立的 key 理论上更好，但实用中 1 个已足够
            //   (一个 1080p 帧有 2M+ 像素，整个序列周期足够长)
            pixel[0] ^= key;   // Blue
            pixel[1] ^= key;   // Green
            pixel[2] ^= key;   // Red
        }
    }
}
