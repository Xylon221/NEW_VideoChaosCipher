#ifndef STATS_H
#define STATS_H

#include <atomic>
#include <opencv2/opencv.hpp>

// ============================================================
// stats.h — 全项目共享的数据结构定义
// ============================================================
// 本文件是整个项目的"数据契约"层，定义了：
//   FrameData  — 在 Reader → Encryptor → Writer 三阶段之间流动的帧数据包
//   BenchStats — 性能与运行时统计，所有线程通过引用共享
//
// 为什么放在单独的 header:
//   读者/加密者/写者三个线程模块都需要这两个结构体，
//   拆出来避免循环依赖，也方便 tests 复用。
// ============================================================

// ---- 帧数据包 ----
// Reader 从视频文件读出一帧，包上帧号，塞入读队列；
// Encryptor 从读队列取出，原地加密后塞入写队列；
// Writer 从写队列取出，按帧号排序后写入输出文件。
struct FrameData
{
    cv::Mat frame;       // 帧像素数据 (BGR, CV_8UC3, 由 VPUDecoder::read 保证)
    int frame_index;     // 帧序号，从 0 开始递增；Writer 靠它做乱序重排
};

// ---- 性能统计 ----
// 所有字段要么是 atomic<int> (多线程无锁读/写)，要么只在主线程写入后读取。
// 注意: readQueueDepth / writeQueueDepth 是由 reader/encryptor/writer 线程
// 各自 ++ / -- 维护的瞬时值，仅在主线程打印报告时读取。
struct BenchStats
{
    // 读队列深度跟踪：reader ++, encryptor --
    std::atomic<int> readQueueDepth{0};
    std::atomic<int> readQueuePeak{0};   // 读队列历史最大深度

    // 写队列深度跟踪：encryptor ++, writer --
    std::atomic<int> writeQueueDepth{0};
    std::atomic<int> writeQueuePeak{0};  // 写队列历史最大深度

    // 各阶段处理帧数，便于判断瓶颈落在解码、加密还是编码。
    std::atomic<int> totalFramesDecoded{0};
    std::atomic<int> totalFramesEncrypted{0};
    std::atomic<int> totalFramesWritten{0};

    // 以下三个由主线程在 join 后计算，非 atomic 即可
    double readerTimeMs  = 0;   // 读者线程从启动到退出的墙上时间
    double encryptTimeMs = 0;   // 所有加密线程从启动到全部退出的墙上时间
    double writerTimeMs  = 0;   // 写者线程从启动到退出的墙上时间
};

#endif
