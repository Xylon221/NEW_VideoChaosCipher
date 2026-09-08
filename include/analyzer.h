#ifndef ANALYZER_H
#define ANALYZER_H

#include <opencv2/opencv.hpp>

// ============================================================
// analyzer.h — 加密质量评估指标
// ============================================================
// 4 类 5 项指标的数学直觉:
//
//   信息熵 (Entropy):
//     衡量像素值的"混乱程度"。均匀分布时熵最大 = 8.0 bit。
//     原视频帧通常 5~7 bit，加密后应趋近 8.0。
//
//   直方图方差 (Histogram Variance):
//     衡量直方图偏离均匀分布的程度。方差越小越均匀。
//     好的加密算法应该让直方图几乎"平坦"。
//
//   相邻像素相关系数 (Correlation):
//     原图的相邻像素高度相关 (系数接近 0.9~1.0)，
//     加密后应完全去相关 (系数接近 0)。
//     分水平/垂直/对角线三个方向独立测量。
//
//   差分攻击指标 (NPCR + UACI):
//     衡量"改一个像素 → 密文变化多大"。
//     NPCR (Number of Pixel Change Rate) 理想 > 99.6%
//     UACI (Unified Average Change Intensity) 理想 ≈ 33.3%
// ============================================================

// 计算图像的信息熵 (Shannon Entropy)，理想加密图像趋近 8.0 bit
float calcEntropy(const cv::Mat &frame);

// 计算直方图方差，值越小说明像素分布越均匀
float calcHistogramVariance(const cv::Mat &frame);

// 计算相邻像素相关系数 (Pearson)
// direction: "horizontal", "vertical", "diagonal"
// 返回 [-1, 1] 区间，原始图像 > 0.9，加密图像应接近 0
float calcCorrelation(const cv::Mat &frame, const std::string &direction);

// NPCR: 像素改变率，明文改 1 个像素后密文不同的比例，理想 > 99.6%
float calcNPCR(const cv::Mat &original, const cv::Mat &encrypted);

// UACI: 统一平均变化强度，理想约 33.3%
float calcUACI(const cv::Mat &original, const cv::Mat &encrypted);

// 打印完整的加密质量报告 (调用上述所有指标)
void printQualityReport(const cv::Mat &original, const cv::Mat &encrypted);

#endif
