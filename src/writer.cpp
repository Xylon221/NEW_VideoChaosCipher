#include "writer.h"
#include <iostream>
#include <map>

// ============================================================
// writer.cpp — 视频帧写入线程
// ============================================================
// 这是 pipeline 的第三阶段: Consumer
//
// 工作流:
//   while (writeQueue.pop(data)) {    ← 从队列取帧 (阻塞等待)
//     reorderBuffer[frame_index] = data  ← 插入排序缓冲区
//     while (队首帧号 == nextToWrite) {  ← 乱序重排
//       writer.write(frame)              ← VPU 编码写入
//       pop from buffer
//       nextToWrite++
//     }
//   }
//   冲刷残留帧 (
// ============================================================
// 乱序重排 (Reorder Buffer) 详解:
//
//   为什么需要? 多个 encryptor 线程并行处理，完成顺序不确定:
//     Encryptor-0 处理帧 #0 (大帧,慢)  → 写完帧 #0 到 writeQueue
//     Encryptor-1 处理帧 #1 (小帧,快)  → 先写完帧 #1 到 writeQueue
//   如果 Writer 不加处理直接写 → 输出帧序错误
//
//   解决方法: std::map<int, FrameData> 做重排缓冲
//     - map 自动按 key (帧号) 排序
//     - 仅当队首帧号 = 下一个期待的帧号时才写出
//     - 这样即使乱序到达，输出始终有序
//
//   内存占用: 最坏情况下所有帧都在 buffer 中等候 (与 writeQueue 元素总数相当)
//   但实际中帧处理速度差异不大，buffer 通常只有 1~3 帧
// ============================================================

void writerThread(VPUEncoder &writer, SafeQueue<FrameData> &writeQueue,
                  BenchStats &stats) {
    std::cout << "Write started." << std::endl;

    FrameData data;
    int nextToWrite = 0;                      // 下一个期待的帧号
    std::map<int, FrameData> reorderBuffer;   // 乱序缓冲 (帧号 → 帧数据)
    int index = 0;                            // 实际写入帧数

    // ---- 主循环: 从队列取帧 ----
    while (writeQueue.pop(data)) {
        stats.writeQueueDepth.store(static_cast<int>(writeQueue.size()));

        // 插入缓冲 (map 自动按帧号排序)
        reorderBuffer[data.frame_index] = std::move(data);

        // 尽可能多地写出连续的帧
        // 条件: 缓冲区非空 且 队首帧号恰好是下一个期待的帧号
        while (!reorderBuffer.empty() &&
               reorderBuffer.begin()->first == nextToWrite) {
            // 写出到 VPU 编码器
            writer.write(reorderBuffer.begin()->second.frame);
            ++stats.totalFramesWritten;
            reorderBuffer.erase(reorderBuffer.begin());
            nextToWrite++;
            index++;
        }
    }

    // ---- 队列关闭后: 冲刷缓冲中的残留帧 ----
    // 这些是已经取了但还没写到文件的帧 (中断/EOF 场景)
    for (auto &pair : reorderBuffer) {
        writer.write(pair.second.frame);
        ++stats.totalFramesWritten;
        index++;
    }

    std::cout << "Write finished, totally write " << index << " frame" << std::endl;
}
