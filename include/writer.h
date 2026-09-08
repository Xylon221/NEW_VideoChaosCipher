#ifndef WRITER_H
#define WRITER_H

#include "SafeQueue.h"
#include "vpu_io.h"
#include "stats.h"

// ============================================================
// writer.h — 视频帧写入线程
// ============================================================
// 职责:
//   1. 从 writeQueue 取帧 (多个 Encryptor 并行推入，到达顺序随机)
//   2. 按 frame_index 重排序 (因为多线程加密后帧可能乱序到达)
//   3. 顺序写入 VPUEncoder → 输出文件
//
// 乱序重排 (reorder buffer):
//   Writer 用 std::map<int, FrameData> 做缓冲区:
//     - 收到的帧按 frame_index 插入 map (自动按 key 排序)
//     - 每次检查队首: 如果是最小的未写入帧号 → 弹出并写入
//     - 保证输出视频帧顺序正确
//
//   例: 收到帧 {2, 1, 0, 4, 3}:
//     收到2 → buffer={2}, next=0, 不写入
//     收到1 → buffer={1,2}, next=0, 不写入
//     收到0 → buffer={0,1,2}, next=0 → 连续写出0,1,2 → next=3
//     收到4 → buffer={4}, next=3, 不写入
//     收到3 → buffer={3,4}, next=3 → 连续写出3,4 → next=5
// ============================================================

void writerThread(VPUEncoder &writer, SafeQueue<FrameData> &writeQueue,
                  BenchStats &stats);

#endif
