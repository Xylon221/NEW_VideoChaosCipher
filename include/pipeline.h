#ifndef PIPELINE_H
#define PIPELINE_H

#include <string>
#include <cstddef>
#include "SafeQueue.h"
#include "stats.h"

// ============================================================
// pipeline.h — 多线程视频处理管线入口
// ============================================================
// 架构: 三段式流水线 (Producer-Transformer-Consumer)
//
//   Reader 线程 (1个)
//     │ VPU 硬件解码 → FrameData → readQueue.push()
//     ↓
//   Encryptor 线程 (N个, N=numThreads)
//     │ readQueue.pop() → encryptFrame() → writeQueue.push()
//     ↓
//   Writer 线程 (1个)
//     │ writeQueue.pop() → 按帧号排序 → VPU 硬件编码写入
//
// 为什么这样设计:
//   - Reader/Writer 是 IO 密集型 → 各 1 个线程就够了
//   - Encrypt 是 CPU 密集型 → 多线程并行，充分利用多核
//   - 队列解耦了三个阶段，速度不匹配时队列自然缓冲
//
// 阅读顺序建议: stats.h → SafeQueue.h → encryptor.h/cpp → 本文件
// ============================================================

// 加密工作线程函数
// readQueue:  上游读者提供的帧队列
// writeQueue: 下游写者消费的帧队列
// start_idx / end_idx: 需要加密的帧号范围 [start, end] (闭区间)
//                      超出此范围的帧直接传递不加密
// seed:       混沌种子
// threadId:   线程编号 (仅用于打印日志)
// stats:      性能统计引用
void encryptThread(SafeQueue<FrameData> &readQueue,
                   SafeQueue<FrameData> &writeQueue,
                   int start_idx, int end_idx, float seed, int threadId,
                   BenchStats &stats);

// 视频处理主流程
// 1. 打开输入/输出文件
// 2. 创建 reader / N encryptors / writer 线程
// 3. 等待全部线程结束
// 4. 打印性能报告
// 返回 true 表示处理成功
bool processVideo(const std::string &input_path, const std::string &output_path,
                  float start_sec, float end_sec, float seed, int numThreads,
                  std::size_t queueCapacity = 8);

#endif
