#include "analyzer.h"
#include <cmath>
#include <vector>
#include <iostream>
#include <iomanip>

// ============================================================
// analyzer.cpp — 加密质量评估指标实现
// ============================================================
// 5 个指标从不同角度评估加密算法的安全性:
//
//   指标              测试目标              理想值        为什么
//   ─────            ──────────            ──────        ─────
//   Entropy          统计分布均匀性         8.0 bit       均匀分布 = 不可区分
//   Histogram Var    直方图平坦度           < 500         平坦 = 无法统计攻击
//   Correlation      像素空间关系           ≈ 0           邻居不相关 = 不可预测
//   NPCR             改 1 像素后的密文变化  > 99.6%       抵抗差分攻击
//   UACI             平均变化强度           ≈ 33.3%       抵抗差分攻击
//
// 使用注意:
//   - 这些是静态 (single-frame) 指标，不衡量帧间关系
//   - 对视频加密来说，还需额外考虑帧间相关性 (本项目的加密每帧独立，
//     帧间相关性自然被破坏)
// ============================================================

// ---- 信息熵 (Shannon Entropy) ----
// 公式: H = -Σ p(k) * log2(p(k))   k ∈ [0, 255]
// p(k) 是灰度级 k 的出现概率。
// 如果 256 个灰度级完全均匀分布: p(k) = 1/256, H = 8.0 bit
// 如果所有像素同一个灰度: p(某k)=1, 其余=0, H = 0 bit
// 对 BGR 三通道分别计算然后取平均
float calcEntropy(const cv::Mat &frame) {
    // 统计各通道每个灰度级 (0-255) 的出现次数
    int hist[3][256] = {};   // hist[channel][intensity]
    int totalPixels = frame.rows * frame.cols;

    for (int i = 0; i < frame.rows; ++i) {
        for (int j = 0; j < frame.cols; ++j) {
            const cv::Vec3b &p = frame.at<cv::Vec3b>(i, j);
            hist[0][p[0]]++;   // B 通道
            hist[1][p[1]]++;   // G 通道
            hist[2][p[2]]++;   // R 通道
        }
    }

    // 对每个通道分别计算熵，取平均值
    float entropySum = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float h = 0.0f;
        for (int k = 0; k < 256; ++k) {
            if (hist[c][k] == 0) continue;   // log2(0) 无定义，概率为 0 则跳过
            float p = static_cast<float>(hist[c][k]) / totalPixels;
            h -= p * std::log2(p);   // Shannon 公式
        }
        entropySum += h;
    }
    // 返回三通道平均熵值
    return entropySum / 3.0f;
}

// ---- 直方图方差 ----
// 衡量直方图偏离均匀分布的程度。
// expected = totalPixels / 256 = 理想均匀分布时每个灰度级的像素数
// variance = Σ (实际值 - 期望值)^2 / (3 * 256)   (三通道归一化)
// 完全平坦的直方图: variance ≈ 0
// 有明显峰值的直方图: variance 很大 (原图通常 > 10000)
float calcHistogramVariance(const cv::Mat &frame) {
    int totalPixels = frame.rows * frame.cols;
    float expected = static_cast<float>(totalPixels) / 256.0f;  // 理想均匀分布值

    // 统计直方图
    int hist[3][256] = {};
    for (int i = 0; i < frame.rows; ++i) {
        for (int j = 0; j < frame.cols; ++j) {
            const cv::Vec3b &p = frame.at<cv::Vec3b>(i, j);
            hist[0][p[0]]++;
            hist[1][p[1]]++;
            hist[2][p[2]]++;
        }
    }

    // 计算方差: Σ(observed - expected)^2
    float varSum = 0.0f;
    for (int c = 0; c < 3; ++c) {
        for (int k = 0; k < 256; ++k) {
            float diff = static_cast<float>(hist[c][k]) - expected;
            varSum += diff * diff;
        }
    }
    // 归一化：除以 (通道数 * 灰度级数)，便于跨分辨率比较
    return varSum / (3.0f * 256.0f);
}

// ---- 相邻像素相关系数 (Pearson Correlation) ----
// 随机采样 N=3000 对相邻像素，计算线性相关系数。
// 原理: 自然图像的相邻像素颜色接近 → 相关性高 (> 0.9)
//       加密后像素值随机 → 邻居之间无相关性 (≈ 0)
//
// 方向参数:
//   "horizontal" → (x, y) vs (x+1, y)
//   "vertical"   → (x, y) vs (x, y+1)
//   "diagonal"   → (x, y) vs (x+1, y+1)
float calcCorrelation(const cv::Mat &frame, const std::string &direction) {
    const int N = 3000;   // 采样数量，3000 足够大且计算快
    std::vector<int> xVals, yVals;

    // 根据方向设置偏移量
    int dx = 0, dy = 0;
    if (direction == "horizontal")  dx = 1;
    if (direction == "vertical")    dy = 1;
    if (direction == "diagonal")  { dx = 1; dy = 1; }

    // 随机采样 N 对像素
    for (int k = 0; k < N; ++k) {
        // 确保采样位置不会越界 (留出偏移量)
        int i = std::rand() % (frame.rows - dy);
        int j = std::rand() % (frame.cols - dx);

        // 取灰度值 (R+G+B)/3 作为像素强度 (简化处理)
        const cv::Vec3b &p1 = frame.at<cv::Vec3b>(i, j);
        const cv::Vec3b &p2 = frame.at<cv::Vec3b>(i + dy, j + dx);

        int v1 = (p1[0] + p1[1] + p1[2]) / 3;
        int v2 = (p2[0] + p2[1] + p2[2]) / 3;

        xVals.push_back(v1);
        yVals.push_back(v2);
    }

    // 计算 Pearson 相关系数: r = Cov(X,Y) / sqrt(Var(X) * Var(Y))
    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
    int n = static_cast<int>(xVals.size());
    for (int i = 0; i < n; ++i) {
        sumX  += xVals[i];
        sumY  += yVals[i];
        sumXY += xVals[i] * yVals[i];
        sumX2 += xVals[i] * xVals[i];
        sumY2 += yVals[i] * yVals[i];
    }

    // Pearson 公式的分子和分母
    float numerator = n * sumXY - sumX * sumY;
    float denomX = n * sumX2 - sumX * sumX;   // n * Var(X)
    float denomY = n * sumY2 - sumY * sumY;   // n * Var(Y)

    // 方差为 0 说明全图灰度相同 (纯色图)，返回无效值
    if (denomX <= 0 || denomY <= 0) return -2.0f;

    return numerator / std::sqrt(denomX * denomY);
}

// ---- NPCR (Number of Pixel Change Rate) ----
// 衡量: 明文 1 个像素变化导致密文有多少像素不同。
// 公式: NPCR = (diff_pixel_count / total_pixels) * 100%
//
// 理想值 > 99.6%: 说明 1 个明文像素的改变导致几乎所有密文像素都变了
//   → 攻击者无法通过比较密文差异推断明文信息
//
// 注意: 这里比较的是 original 和 encrypted 两张完整图像，
// 严格说 NPCR 应该比较 "同一位置改一像素前后的两张密文"。
// 本实现作为快速整体质量检查，比较明文和密文的差异率 (应接近 100%)
float calcNPCR(const cv::Mat &original, const cv::Mat &encrypted) {
    if (original.size() != encrypted.size()) return -1.0f;

    int diffCount = 0;
    int totalPixels = original.rows * original.cols;

    for (int i = 0; i < original.rows; ++i) {
        for (int j = 0; j < original.cols; ++j) {
            const cv::Vec3b &a = original.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = encrypted.at<cv::Vec3b>(i, j);
            // 任一通道不同即记为不同像素
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                diffCount++;
            }
        }
    }

    return 100.0f * diffCount / totalPixels;
}

// ---- UACI (Unified Average Change Intensity) ----
// 衡量: 密文变化的幅度有多大。
// 公式: UACI = (1 / (W*H*3*255)) * Σ|C1(i,j,k) - C2(i,j,k)| * 100%
//
// 理想值 ≈ 33.3%: 说明变化幅度足够大且分布均匀
//   → 不是"只有一点点不同"，而是彻底改变了像素值
float calcUACI(const cv::Mat &original, const cv::Mat &encrypted) {
    if (original.size() != encrypted.size()) return -1.0f;

    float sumDiff = 0.0f;
    int totalPixels = original.rows * original.cols;

    for (int i = 0; i < original.rows; ++i) {
        for (int j = 0; j < original.cols; ++j) {
            const cv::Vec3b &a = original.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = encrypted.at<cv::Vec3b>(i, j);
            // 三个通道的绝对差累加 (不是每个通道单独算)
            sumDiff += std::abs(a[0] - b[0])
                     + std::abs(a[1] - b[1])
                     + std::abs(a[2] - b[2]);
        }
    }

    // 分母: 像素数 * 3 通道 * 255 最大差值 = 最大可能的累计差值
    return sumDiff / (totalPixels * 3.0f * 255.0f) * 100.0f;
}

// ---- 完整质量报告 ----
// 一次性运行所有指标并格式化输出，方便快速判断加密质量
void printQualityReport(const cv::Mat &original, const cv::Mat &encrypted) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n====================================================\n";
    std::cout << "        Encryption Quality Report\n";
    std::cout << "====================================================\n\n";

    // --- 原始图像指标 (基线) ---
    std::cout << "[Original Image]\n";
    std::cout << "  Entropy:              " << calcEntropy(original)        << " bit\n";
    std::cout << "  Histogram Variance:   " << calcHistogramVariance(original) << "\n";
    std::cout << "  Correlation (H):      " << calcCorrelation(original, "horizontal") << "\n";
    std::cout << "  Correlation (V):      " << calcCorrelation(original, "vertical")   << "\n";
    std::cout << "  Correlation (D):      " << calcCorrelation(original, "diagonal")   << "\n\n";

    // --- 加密图像指标 (与理想值对比) ---
    std::cout << "[Encrypted Image]\n";
    std::cout << "  Entropy:              " << calcEntropy(encrypted)        << " bit  (ideal: 8.0)\n";
    std::cout << "  Histogram Variance:   " << calcHistogramVariance(encrypted) << "  (ideal: < 500)\n";
    std::cout << "  Correlation (H):      " << calcCorrelation(encrypted, "horizontal") << "  (ideal: ~0)\n";
    std::cout << "  Correlation (V):      " << calcCorrelation(encrypted, "vertical")   << "  (ideal: ~0)\n";
    std::cout << "  Correlation (D):      " << calcCorrelation(encrypted, "diagonal")   << "  (ideal: ~0)\n\n";

    // --- 差分攻击抵抗指标 ---
    std::cout << "[Differential Attack Resistance]\n";
    std::cout << "  NPCR:                 " << calcNPCR(original, encrypted)  << "%  (ideal: > 99.6%)\n";
    std::cout << "  UACI:                 " << calcUACI(original, encrypted)  << "%  (ideal: ~33.3%)\n";

    std::cout << "\n====================================================\n\n";
}
