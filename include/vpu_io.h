#ifndef VPU_IO_H
#define VPU_IO_H

#include <opencv2/opencv.hpp>
#include <string>

struct VPUDecoderImpl;
struct VPUEncoderImpl;

// VPU 硬件加速视频解码器 (FFmpeg + h264_rkmpp / hevc_rkmpp)
// 接口尽量贴近 cv::VideoCapture，减少调用方改动
class VPUDecoder {
public:
    VPUDecoder();
    ~VPUDecoder();

    bool open(const std::string &path);
    bool read(cv::Mat &frame);
    void release();

    double getFPS() const;
    int    getWidth() const;
    int    getHeight() const;
    int    getFrameCount() const;
    bool   isOpened() const;

private:
    VPUDecoderImpl *impl;
};

// VPU 硬件加速视频编码器 (FFmpeg + h264_v4l2m2m / hevc_v4l2m2m)
// 接口尽量贴近 cv::VideoWriter
class VPUEncoder {
public:
    VPUEncoder();
    ~VPUEncoder();

    bool open(const std::string &path, int width, int height, double fps);
    bool write(const cv::Mat &frame);
    void release();
    bool isOpened() const;

private:
    VPUEncoderImpl *impl;
};

#endif
