#ifndef READER_H
#define READER_H

#include "SafeQueue.h"
#include "vpu_io.h"
#include "stats.h"

// ============================================================
// reader.h — 视频帧读取线程
// ============================================================
// 职责:
//   1. 循环调用 VPUDecoder::read() 逐帧解码
//   2. 包装为 FrameData{frame, frame_index} 推入 readQueue
//   3. 读完所有帧后调用 readQueue.setFinished() 通知下游
//
// 特殊设计点:
//   - 单 producer (Reader) + 多 consumer (Encryptors)，所以 finished 信号
//     只由 Reader 发送一次，避免重复通知导致数据竞争
//   - 解码失败则 break（异常帧被丢弃，不停止整个管线）
// ============================================================

void readerThread(VPUDecoder &dec, SafeQueue<FrameData> &readQueue,
                  BenchStats &stats);

#endif
