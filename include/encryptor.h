#ifndef ENCRYPTOR_H
#define ENCRYPTOR_H
#include <opencv2/opencv.hpp>
#include <vector>
float logisticMap(float x, float r = 3.999f);

void encryptFrame(cv::Mat &frame);


#endif