#include <iostream>
#include <opencv2/opencv.hpp>
#include <vector>
#include <random>

struct FrameData
{
    cv::Mat frame;
    int frame_index;
};

// Logistic map 混沌映射函数
float logisticMap(float x, float r = 3.999f) {
    return r * x * (1 - x);
}

// 使用混沌序列加密图片（通过异或操作）
void chaosEncrypt(cv::Mat& image) {
    int rows = image.rows;
    int cols = image.cols;

    // 将图片转化为一维像素数组
    std::vector<cv::Vec3b> pixels(rows * cols);
    
    for (int i = 0; i < rows; ++i) {
        for (int j = 0; j < cols; ++j) {
            pixels[i * cols + j] = image.at<cv::Vec3b>(i, j);
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
            image.at<cv::Vec3b>(i, j) = pixels[i * cols + j];
        }
    }
}

int main() {
    printf("Hello World\n");

    // 读取图片
    cv::Mat image = cv::imread("/home/orangepi/Work/VideoChaosCipher/frame10.jpg");
    if (image.empty()) {
        std::cout << "错误: 图片未打开" << std::endl;
        return -1;
    }

    // 对图片进行混沌加密
    chaosEncrypt(image);

    // 保存加密后的图片
    cv::imwrite("/home/orangepi/Work/VideoChaosCipher/encrypted_frame10.jpg", image);

    std::cout << "加密后的图片已保存。" << std::endl;
    return 0;
}
