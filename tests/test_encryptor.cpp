#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>
#include "/home/orangepi/Work/VideoChaosCipher/include/encryptor.h"

// 构造一张纯色测试图
static cv::Mat makeSolidImage(int w, int h, uchar b, uchar g, uchar r) {
    cv::Mat img(h, w, CV_8UC3);
    for (int i = 0; i < h; ++i) {
        for (int j = 0; j < w; ++j) {
            img.at<cv::Vec3b>(i, j) = cv::Vec3b(b, g, r);
        }
    }
    return img;
}

// 加密后再解密应还原原始图像
TEST(EncryptorTest, Roundtrip) {
    cv::Mat original = makeSolidImage(100, 100, 50, 100, 150);

    cv::Mat encrypted = original.clone();
    encryptFrame(encrypted, 0.5f);   // 加密
    encryptFrame(encrypted, 0.5f);   // 解密（相同 seed）

    // 逐像素对比，差异应为 0
    for (int i = 0; i < 100; ++i) {
        for (int j = 0; j < 100; ++j) {
            const cv::Vec3b &a = original.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = encrypted.at<cv::Vec3b>(i, j);
            EXPECT_EQ(a[0], b[0]) << "B channel mismatch at (" << i << "," << j << ")";
            EXPECT_EQ(a[1], b[1]) << "G channel mismatch at (" << i << "," << j << ")";
            EXPECT_EQ(a[2], b[2]) << "R channel mismatch at (" << i << "," << j << ")";
        }
    }
}

// 不同 seed 应产生不同的加密结果
TEST(EncryptorTest, DifferentSeedsProduceDifferentResults) {
    cv::Mat original = makeSolidImage(100, 100, 50, 100, 150);

    cv::Mat enc1 = original.clone();
    cv::Mat enc2 = original.clone();
    encryptFrame(enc1, 0.5f);
    encryptFrame(enc2, 0.5001f);  // 微小差异的 seed

    // 统计像素差异数，应该很大（> 90% 像素不同）
    int diffCount = 0;
    int total = 100 * 100;
    for (int i = 0; i < 100; ++i) {
        for (int j = 0; j < 100; ++j) {
            const cv::Vec3b &a = enc1.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = enc2.at<cv::Vec3b>(i, j);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                diffCount++;
            }
        }
    }

    float diffRate = 100.0f * diffCount / total;
    EXPECT_GT(diffRate, 90.0f)
        << "Different seeds should produce very different results, "
        << "but only " << diffRate << "% pixels differ";
}

// 加密后图像应与原始明显不同
TEST(EncryptorTest, EncryptionChangesImage) {
    cv::Mat original = makeSolidImage(100, 100, 50, 100, 150);
    cv::Mat encrypted = original.clone();
    encryptFrame(encrypted, 0.5f);

    int diffCount = 0;
    int total = 100 * 100;
    for (int i = 0; i < 100; ++i) {
        for (int j = 0; j < 100; ++j) {
            const cv::Vec3b &a = original.at<cv::Vec3b>(i, j);
            const cv::Vec3b &b = encrypted.at<cv::Vec3b>(i, j);
            if (a[0] != b[0] || a[1] != b[1] || a[2] != b[2]) {
                diffCount++;
            }
        }
    }

    float diffRate = 100.0f * diffCount / total;
    EXPECT_GT(diffRate, 90.0f) << "Encryption should change most pixels";
}
