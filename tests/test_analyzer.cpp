#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>
#include <cmath>
#include "/home/orangepi/Work/VideoChaosCipher/include/encryptor.h"
#include "/home/orangepi/Work/VideoChaosCipher/include/analyzer.h"

// 构造纯色图像
static cv::Mat makeSolidImage(int w, int h, uchar b, uchar g, uchar r) {
    cv::Mat img(h, w, CV_8UC3);
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            img.at<cv::Vec3b>(i, j) = cv::Vec3b(b, g, r);
        }
    }
    return img;
}

// 完全均匀的图像（所有像素相同）→ 熵应为 0
TEST(AnalyzerTest, UniformImageEntropy) {
    cv::Mat img = makeSolidImage(128, 128, 100, 100, 100);
    float entropy = calcEntropy(img);
    EXPECT_NEAR(entropy, 0.0f, 0.01f);
}

// 加密后熵应显著增加（理想接近 8.0）
TEST(AnalyzerTest, EncryptedImageHighEntropy) {
    cv::Mat original = makeSolidImage(128, 128, 50, 100, 150);
    cv::Mat encrypted = original.clone();
    encryptFrame(encrypted, 0.5f);

    float origEntropy = calcEntropy(original);
    float encEntropy  = calcEntropy(encrypted);

    EXPECT_NEAR(origEntropy, 0.0f, 0.01f);
    EXPECT_GT(encEntropy, 7.0f);  // 加密后应接近 8
}

// 原始图像相邻像素高度相关
TEST(AnalyzerTest, OriginalImageHighCorrelation) {
    cv::Mat img = makeSolidImage(200, 200, 0, 0, 0);
    // 在纯色图上画一些渐变
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            uchar val = static_cast<uchar>((i + j) % 256);
            img.at<cv::Vec3b>(i, j) = cv::Vec3b(val, val, val);
        }
    }
    float corr = calcCorrelation(img, "horizontal");
    EXPECT_GT(corr, 0.5f);  // 渐变图相邻像素高度相关
}

// 加密后相邻像素相关性应接近 0
TEST(AnalyzerTest, EncryptedImageLowCorrelation) {
    cv::Mat original = makeSolidImage(200, 200, 50, 100, 150);
    cv::Mat encrypted = original.clone();
    encryptFrame(encrypted, 0.5f);

    float corrH = calcCorrelation(encrypted, "horizontal");
    float corrV = calcCorrelation(encrypted, "vertical");
    float corrD = calcCorrelation(encrypted, "diagonal");

    EXPECT_NEAR(corrH, 0.0f, 0.1f);
    EXPECT_NEAR(corrV, 0.0f, 0.1f);
    EXPECT_NEAR(corrD, 0.0f, 0.1f);
}

// NPCR: 同一图像 vs 加密图像 → 几乎所有像素应改变
TEST(AnalyzerTest, NPCR) {
    cv::Mat original = makeSolidImage(200, 200, 50, 100, 150);
    cv::Mat encrypted = original.clone();
    encryptFrame(encrypted, 0.5f);

    float npcr = calcNPCR(original, encrypted);
    EXPECT_GT(npcr, 99.0f);  // NPCR 应 > 99%
}
