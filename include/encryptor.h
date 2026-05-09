#ifndef ENCRYPTOR_H
#define ENCRYPTOR_H
#include <opencv2/opencv.hpp>
#include <vector>
float logisticMap(float x, float r = 3.999f);

// seed: 混沌映射初始种子，同一 seed 加密/解密结果一致，默认 0.5
void encryptFrame(cv::Mat &frame, float seed = 0.5f);


#endif