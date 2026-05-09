#ifndef ANALYZER_H
#define ANALYZER_H

#include <opencv2/opencv.hpp>

// 计算图像的信息熵 (Shannon Entropy)，理想加密图像趋近 8.0
float calcEntropy(const cv::Mat &frame);

// 计算直方图方差，值越小说明像素分布越均匀
float calcHistogramVariance(const cv::Mat &frame);

// 计算相邻像素相关系数 (Pearson)
// direction: "horizontal", "vertical", "diagonal"
float calcCorrelation(const cv::Mat &frame, const std::string &direction);

// NPCR: 像素改变率，明文改 1 个像素后密文不同的比例，理想 > 99.6%
float calcNPCR(const cv::Mat &original, const cv::Mat &encrypted);

// UACI: 统一平均变化强度，理想约 33.3%
float calcUACI(const cv::Mat &original, const cv::Mat &encrypted);

// 打印完整的加密质量报告
void printQualityReport(const cv::Mat &original, const cv::Mat &encrypted);

#endif
