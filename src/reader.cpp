#include "reader.h"
#include <iostream>

// ============================================================
// reader.cpp — 视频帧读取线程
// ============================================================
// 这是 pipeline 的第一阶段: Producer
//
// 工作流:
//   循环 {
//     dec.read(frame)           ← FFmpeg 硬件解码一帧 (BGR24)
//     FrameData{frame, index}   ← 包上帧号
//     readQueue.push(data)      ← 推入队列 (可能阻塞 encryptor 消费)
//   }
//   readQueue.setFinished()     ← 通知所有消费者"我读完了"
//
// 性能考量:
//   - Reader 是单线程的，因为解码器本身不是线程安全的
//   - 解码速度远快于加密速度 (尤其是软件解码时)，所以读队列会积压
//     这是设计预期的——队列自然做缓冲，避免解码等待
//
// 容错:
//   - 解码失败直接 break（不停止管线，但跳过的帧会丢失）
//   - 不会因为单帧解码失败而崩溃
// ============================================================

void readerThread(VPUDecoder &dec, SafeQueue<FrameData> &readQueue,
                   BenchStats &stats) {
    std::cout << "Read started." << std::endl;
    int index = 0;

    while (true) {
        FrameData data;

        // 解码下一帧 → BGR24 cv::Mat
        if (!dec.read(data.frame)) {
            break;   // EOF 或解码失败 → 退出循环
        }

        data.frame_index = index++;   // 帧号从 0 开始自增
        ++stats.totalFramesDecoded;

        if (!readQueue.push(data)) break;   // 推入队列，唤醒一个等待的加密线程

        // 入队可能因有界队列阻塞，入队后读取实际 size 才是准确深度。
        int d = static_cast<int>(readQueue.size());
        stats.readQueueDepth.store(d);
        if (d > stats.readQueuePeak.load()) {
            stats.readQueuePeak.store(d);
        }
    }

    // 全部帧读完 → 发送 finished 信号
    // 这会通知所有阻塞在 readQueue.pop() 上的 encryptor 线程醒来并退出
    readQueue.setFinished();
    std::cout << "Read finished, totally read " << index << " frame" << std::endl;
}
