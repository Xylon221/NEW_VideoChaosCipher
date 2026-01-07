#include "/home/orangepi/Work/VideoChaosCipher/include/encryptor.h"

// Logistic map 混沌映射函数
float logisticMap(float x, float r) {
    return r * x * (1 - x);
}

// 使用混沌序列加密图片（通过异或操作）
void encryptFrame(cv::Mat &frame) {
    int rows = frame.rows;
    int cols = frame.cols;

    // 将图片转化为一维像素数组
    std::vector<cv::Vec3b> pixels(rows * cols);
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            pixels[i * cols + j] = frame.at<cv::Vec3b>(i, j);
        }
    }

    // 随机生成混沌序列（使用logistic映射）
    float chaosValue = 0.5f;  // 初始值
    std::vector<int> chaosSequence(rows * cols);

    // 生成混沌序列
    for (int i = 0; i < rows * cols; ++i) {
        chaosValue = logisticMap(chaosValue);
        chaosSequence[i] = static_cast<int>(chaosValue * 256); // [0, 255] 范围
    }

    // 对每个像素的RGB值进行异或操作
    for (int i = 0; i < rows * cols; ++i) {
        // 通过混沌序列的值与RGB的每个通道进行异或操作
        pixels[i][0] ^= chaosSequence[i];  // Red 通道
        pixels[i][1] ^= chaosSequence[i];  // Green 通道
        pixels[i][2] ^= chaosSequence[i];  // Blue 通道
    }

    // 将加密后的像素写回图像
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            frame.at<cv::Vec3b>(i, j) = pixels[i * cols + j];
        }
    }
}
