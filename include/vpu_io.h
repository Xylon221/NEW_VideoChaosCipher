#ifndef VPU_IO_H
#define VPU_IO_H

#include <opencv2/opencv.hpp>
#include <string>

// ============================================================
// vpu_io.h — VPU 硬件加速视频解码/编码接口
// ============================================================
// 为什么不用 OpenCV 的 cv::VideoCapture / cv::VideoWriter？
//   1. OpenCV 的 Video I/O 走 FFmpeg 软件解码，无硬件加速
//   2. 在 RK3588 嵌入式平台上，h264_rkmpp / h264_v4l2m2m 硬件编解码
//     可以加速 2-4 倍，且解放 CPU 给加密线程
//   3. FFmpeg API 直接提供更细粒度的控制 (线程数、码率、GOP 等)
//
// 设计原则:
//   - 接口尽量贴近 cv::VideoCapture / cv::VideoWriter (少改调用方代码)
//   - 实现细节隐藏在 VPUDecoderImpl / VPUEncoderImpl 中 (Pimpl 模式)
//     → 调用方不 include FFmpeg 头文件，编译隔离
//   - 编解码失败静默跳过，不抛异常 (pipeline 需要容错)
//
// 关于 Pimpl 模式:
//   VPUDecoderImpl 定义在 vpu_io.cpp 中，是一个纯 C 结构体，
//   持有所有 FFmpeg 上下文指针。调用方只知道 VPUDecoder 有 impl 指针。
//   好处: header 不暴露 FFmpeg 类型，编译快，接口稳定。
// ============================================================

struct VPUDecoderImpl;   // 前置声明，定义在 .cpp 中 (FFmpeg 解码数据结构)
struct VPUEncoderImpl;   // 前置声明，定义在 .cpp 中 (FFmpeg 编码数据结构)

// ---- VPU 硬件加速视频解码器 ----
// 内部使用 FFmpeg + ARM NEON 软件解码 (h264_rkmpp 在 FFmpeg 4.4 有 bug)
// 接口贴近 cv::VideoCapture
class VPUDecoder {
public:
    VPUDecoder();
    ~VPUDecoder();

    // 打开视频文件，初始化解码器
    bool open(const std::string &path);

    // 读取下一帧，解码为 BGR24 格式的 cv::Mat
    // 返回 true 表示成功，false 表示 EOF 或出错
    bool read(cv::Mat &frame);

    // 释放所有 FFmpeg 资源
    void release();

    double getFPS() const;        // 视频帧率 (如 30.0)
    int    getWidth() const;      // 视频宽度 (像素)
    int    getHeight() const;     // 视频高度 (像素)
    int    getFrameCount() const; // 总帧数 (元数据，可能不准)
    bool   isOpened() const;      // 是否已成功打开
    bool   isHardwareAccelerated() const; // 当前解码器是否为 rkmpp/v4l2m2m
    std::string getCodecName() const;     // 实际打开的解码器名称

private:
    VPUDecoderImpl *impl;   // 私有实现指针 (Pimpl idiom)
};

// ---- VPU 硬件加速视频编码器 ----
// 内部使用 FFmpeg + libx264 (优先尝试 rkmpp/v4l2m2m 硬件编码，失败回退)
// 接口贴近 cv::VideoWriter
class VPUEncoder {
public:
    VPUEncoder();
    ~VPUEncoder();

    // 创建输出文件，指定分辨率与帧率
    // path: 输出文件路径 (.mp4)
    // width, height: 帧分辨率 (必须与实际写入帧一致)
    // fps: 输出帧率
    bool open(const std::string &path, int width, int height, double fps);

    // 写入一帧 (BGR24 cv::Mat)，内部转换为 YUV420P 后编码
    bool write(const cv::Mat &frame);

    // 冲刷编码缓冲区、写文件尾、释放资源
    void release();

    bool isOpened() const;
    bool isHardwareAccelerated() const; // 当前编码器是否为 rkmpp/v4l2m2m
    std::string getCodecName() const;   // 实际打开的编码器名称

private:
    VPUEncoderImpl *impl;
};

#endif
