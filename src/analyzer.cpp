#include "/home/orangepi/Work/VideoChaosCipher/include/analyzer.h"
#include <cmath>
#include <vector>
#include <iostream>
#include <iomanip>

// ---- 信息熵 ----
float calcEntropy(const cv::Mat &frame) {
    // 统计各通道每个灰度级 (0-255) 的出现频率
    int hist[3][256] = {};  // B, G, R
    int totalPixels = frame.rows * frame.cols;

    for (int i = 0; i < frame.rows; ++i) {
        for (int j = 0; j < frame.cols; ++j) {
            const cv::Vec3b &p = frame.at<cv::Vec3b>(i, j);
            hist[0][p[0]]++;  // B
            hist[1][p[1]]++;  // G
            hist[2][p[2]]++;  // R
        }
    }

    // 对每个通道分别计算熵，取平均值
    float entropySum = 0.0f;
    for (int c = 0; c < 3; ++c) {
        float h = 0.0f;
        for (int k = 0; k < 256; ++k) {
            if (hist[c][k] == 0) continue;
            float p = static_cast<float>(hist[c][k]) / totalPixels;
            h -= p * std::log2(p);
        }
        entropySum += h;
    }
    return entropySum / 3.0f;
}

// ---- 直方图方差 ----
float calcHistogramVariance(const cv::Mat &frame) {
    int totalPixels = frame.rows * frame.cols;
    float expected = static_cast<float>(totalPixels) / 256.0f;  // 理想均匀分布

    int hist[3][256] = {};
    for (int i = 0; i < frame.rows; ++i) {
        for (int j = 0; j < frame.cols; ++j) {
            const cv::Vec3b &p = frame.at<cv::Vec3b>(i, j);
            hist[0][p[0]]++;
            hist[1][p[1]]++;
            hist[2][p[2]]++;
        }
    }

    float varSum = 0.0f;
    for (int c = 0; c < 3; ++c) {
        for (int k = 0; k < 256; ++k) {
            float diff = static_cast<float>(hist[c][k]) - expected;
            varSum += diff * diff;
        }
    }
    // 归一化：除以 (通道数 * 灰度级数)
    return varSum / (3.0f * 256.0f);
}

// ---- 相邻像素相关系数 ----
float calcCorrelation(const cv::Mat &frame, const std::string &direction) {
    // 从图像中随机采样 N 对相邻像素
    const int N = 3000;
    std::vector<int> xVals, yVals;

    int dx = 0, dy = 0;
    if (direction == "horizontal")  dx = 1;
    if (direction == "vertical")    dy = 1;
    if (direction == "diagonal")  { dx = 1; dy = 1; }

    for (int k = 0; k < N; ++k) {
        int i = std::rand() % (frame.rows - dy);
        int j = std::rand() % (frame.cols - dx);

        // 取灰度值 (R+G+B)/3 作为像素强度
        const cv::Vec3b &p1 = frame.at<cv::Vec3b>(i, j);
        const cv::Vec3b &p2 = frame.at<cv::Vec3b>(i + dy, j + dx);

        int v1 = (p1[0] + p1[1] + p1[2]) / 3;
        int v2 = (p2[0] + p2[1] + p2[2]) / 3;

        xVals.push_back(v1);
        yVals.push_back(v2);
    }

    // 计算 Pearson 相关系数
    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0, sumY2 = 0;
    int n = static_cast<int>(xVals.size());
    for (int i = 0; i < n; ++i) {
        sumX  += xVals[i];
        sumY  += yVals[i];
        sumXY += xVals[i] * yVals[i];
        sumX2 += xVals[i] * xVals[i];
        sumY2 += yVals[i] * yVals[i];
    }

    float numerator = n * sumXY - sumX * sumY;
    float denomX = n * sumX2 - sumX * sumX;
    float denomY = n * sumY2 - sumY * sumY;
    if (denomX <= 0 || denomY <= 0) return -2.0f;  // 无效

    return numerator / std::sqrt(denomX * denomY);
}

// ---- NPCR ----
float calcNPCR(const cv::Mat &original, const cv::Mat &encrypted) {
    if (original.size() != encrypted.size()) return -1.0f;

    int diffCount = 0;
    int totalPixels = original.rows * original.cols;

    for (int i = 0; i < original.rows; ++i) {
        for (int j = 0; j < original.cols; ++j) {
            const cv::Vec3b &a = original.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = encrypted.at<cv::Vec3b>(i, j);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                diffCount++;
            }
        }
    }

    return 100.0f * diffCount / totalPixels;
}

// ---- UACI ----
float calcUACI(const cv::Mat &original, const cv::Mat &encrypted) {
    if (original.size() != encrypted.size()) return -1.0f;

    float sumDiff = 0.0f;
    int totalPixels = original.rows * original.cols;

    for (int i = 0; i < original.rows; ++i) {
        for (int j = 0; j < original.cols; ++j) {
            const cv::Vec3b &a = original.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = encrypted.at<cv::Vec3b>(i, j);
            // 三个通道的绝对差求和
            sumDiff += std::abs(a[0] - b[0])
                     + std::abs(a[1] - b[1])
                     + std::abs(a[2] - b[2]);
        }
    }

    // UACI = (1 / (W*H*3)) * sum(|a-b|/255) * 100%
    return sumDiff / (totalPixels * 3.0f * 255.0f) * 100.0f;
}

// ---- 完整质量报告 ----
void printQualityReport(const cv::Mat &original, const cv::Mat &encrypted) {
    std::cout << std::fixed << std::setprecision(4);
    std::cout << "\n====================================================\n";
    std::cout << "        Encryption Quality Report\n";
    std::cout << "====================================================\n\n";

    // --- 原始图像 ---
    std::cout << "[Original Image]\n";
    std::cout << "  Entropy:              " << calcEntropy(original)        << " bit\n";
    std::cout << "  Histogram Variance:   " << calcHistogramVariance(original) << "\n";
    std::cout << "  Correlation (H):      " << calcCorrelation(original, "horizontal") << "\n";
    std::cout << "  Correlation (V):      " << calcCorrelation(original, "vertical")   << "\n";
    std::cout << "  Correlation (D):      " << calcCorrelation(original, "diagonal")   << "\n\n";

    // --- 加密图像 ---
    std::cout << "[Encrypted Image]\n";
    std::cout << "  Entropy:              " << calcEntropy(encrypted)        << " bit  (ideal: 8.0)\n";
    std::cout << "  Histogram Variance:   " << calcHistogramVariance(encrypted) << "  (ideal: < 500)\n";
    std::cout << "  Correlation (H):      " << calcCorrelation(encrypted, "horizontal") << "  (ideal: ~0)\n";
    std::cout << "  Correlation (V):      " << calcCorrelation(encrypted, "vertical")   << "  (ideal: ~0)\n";
    std::cout << "  Correlation (D):      " << calcCorrelation(encrypted, "diagonal")   << "  (ideal: ~0)\n\n";

    // --- 差分指标 ---
    std::cout << "[Differential Attack Resistance]\n";
    std::cout << "  NPCR:                 " << calcNPCR(original, encrypted)  << "%  (ideal: > 99.6%)\n";
    std::cout << "  UACI:                 " << calcUACI(original, encrypted)  << "%  (ideal: ~33.3%)\n";

    std::cout << "\n====================================================\n\n";
}
