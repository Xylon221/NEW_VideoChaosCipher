#include "pipeline.h"
#include "reader.h"
#include "writer.h"
#include "encryptor.h"
#include "vpu_io.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <vector>

// ============================================================
// pipeline.cpp — 多线程视频处理管线核心
// ============================================================
// 这是项目的"大脑"，负责:
//   1. 组装 Reader → N×Encryptor → Writer 三段流水线
//   2. 创建/销毁线程，管理生命周期
//   3. 收集性能统计，打印报表
//
// 架构图:
//
//   ┌──────────┐    readQueue     ┌──────────────┐    writeQueue    ┌──────────┐
//   │  Reader  │ ──────────────→  │  Encryptor-0 │ ──────────────→  │  Writer  │
//   │ (1线程)  │                  │  Encryptor-1 │                  │ (1线程)  │
//   │ 解码帧   │                  │  Encryptor-2 │                  │ 排序+编码│
//   │          │                  │  ...          │                  │          │
//   └──────────┘                  └──────────────┘                  └──────────┘
//                                     N 个线程并行
//
// 线程生命周期:
//   Reader:  读完所有帧后调用 readQueue.setFinished() → 退出
//   Encryptor: readQueue.pop() 遇到 finished 且队列空 → 退出
//   Writer:   readQueue.setFinished() 后, writeQueue.pop() 遇到 finished → 退出
//
// 关键设计决策:
//   - 为什么不限制队列大小?
//     队列无界 → 可能内存爆炸 (理论上)
//     实际中: 1080p × BGR24 ≈ 6MB/帧, 几百帧不过 1~2GB，安全
//     有界队列 (背压) 会增加复杂性，现有设计简单且够用
// ============================================================

// ---- 打印视频元信息 ----
static void printVideoInfo(VPUDecoder &dec) {
    if (!dec.isOpened()) {
        std::cout << "ERROR: video not opened" << std::endl;
        return;
    }

    int width = dec.getWidth();
    int height = dec.getHeight();
    double fps = dec.getFPS();
    int framecount = dec.getFrameCount();

    std::cout << "Original Video Info:" << std::endl;
    std::cout << "  Resolution: " << width << "x" << height << std::endl;
    std::cout << "  FPS: " << fps  << std::endl;
    std::cout << "  Total frames: " << framecount << std::endl;
}

// ============================================================
// encryptThread — 加密工作线程 (Transformer)
// ============================================================
// 每个线程独立运行，共享 readQueue 和 writeQueue。
//
// 工作流:
//   while (readQueue.pop(data)) {          ← 从读队列取帧 (阻塞等待)
//     if (帧在加密范围内) encryptFrame()   ← 条件加密
//     writeQueue.push(data)                ← 推入写队列
//   }
//   退出
//
// 为什么加密范围是 [start_idx, end_idx] 而非全视频?
//   1. 很多视频开头有纯色/黑屏帧，浪费计算
//   2. 允许用户精确控制加密哪些帧
//   3. 帧范围外的帧直通 (pass-through) → 后续帧的解密也只需处理同范围
//
// 线程安全:
//   - encryptFrame() 原地修改 data.frame，每个线程只修改自己取出的帧
//     不存在数据竞争 (FrameData 在不同线程间通过队列 move 转移所有权)
//   - stats 中的 atomic 字段可以安全的跨线程 ++/--
// ============================================================
void encryptThread(SafeQueue<FrameData> &readQueue,
                   SafeQueue<FrameData> &writeQueue,
                   int start_idx, int end_idx, float seed, int threadId,
                   BenchStats &stats) {
    FrameData data;
    int localEncrypted = 0;   // 本线程加密的帧数

    // 循环取帧，直到 readQueue 耗尽且 finished
    while (readQueue.pop(data)) {
        stats.readQueueDepth.store(static_cast<int>(readQueue.size()));

        // 仅处理范围内的帧
        if (data.frame_index >= start_idx && data.frame_index <= end_idx) {
            encryptFrame(data.frame, seed);   // 原地加密 (XOR)
            localEncrypted++;
        }
        // 范围外的帧直接透传 (不解密也不加密)

        if (!writeQueue.push(data)) break;

        int d = static_cast<int>(writeQueue.size());
        stats.writeQueueDepth.store(d);
        if (d > stats.writeQueuePeak.load()) {
            stats.writeQueuePeak.store(d);
        }
    }

    // 线程退出前汇总统计
    stats.totalFramesEncrypted += localEncrypted;
    std::cout << "Encryptor " << threadId << " finished, encrypted "
              << localEncrypted << " frames" << std::endl;
}

// ============================================================
// processVideo — 主流程: 打开文件 → 启动管线 → 等待 → 报绩效
// ============================================================
// 返回值: true=成功, false=打开文件失败或 FPS 无效
//
// 调用关系:
//   main.cpp → processVideo() → readerThread / encryptThread / writerThread
//
// 时间测量说明:
//   readerTimeMs:  Reader 线程从启动到 join 的墙上时间
//   encryptTimeMs: 所有 Encryptor 从启动到全部 join 的墙上时间
//   writerTimeMs:  Writer 线程从启动到 join 的墙上时间
//
//   注意: 这三个时间段是重叠的 (pipeline 并发执行)
//   readerTimeMs + encryptTimeMs + writerTimeMs > totalTimeMs
// ============================================================
bool processVideo(const std::string &input_path, const std::string &output_path,
                  float start_sec, float end_sec, float seed, int numThreads,
                  std::size_t queueCapacity) {
    // ---- 1. 打开输入视频 ----
    VPUDecoder dec;
    if (!dec.open(input_path)) {
        std::cerr << "Failed to open input video: " << input_path << std::endl;
        return false;
    }

    printVideoInfo(dec);
    std::cout << "[Runtime] Decoder codec: " << dec.getCodecName()
              << " (HW=" << (dec.isHardwareAccelerated() ? "yes" : "no") << ")" << std::endl;

    int width  = dec.getWidth();
    int height = dec.getHeight();
    double fps = dec.getFPS();

    if (fps <= 0) {
        std::cerr << "ERROR: Invalid FPS" << std::endl;
        return false;
    }

    // ---- 2. 创建输出视频 ----
    VPUEncoder writer;
    if (!writer.open(output_path, width, height, fps)) {
        std::cerr << "Failed to open output video: " << output_path << std::endl;
        return false;
    }
    std::cout << "[Runtime] Encoder codec: " << writer.getCodecName()
              << " (HW=" << (writer.isHardwareAccelerated() ? "yes" : "no") << ")" << std::endl;

    // ---- 3. 创建两个队列 (管线连接器) ----
    SafeQueue<FrameData> readQueue(queueCapacity), writeQueue(queueCapacity);
    BenchStats stats;

    std::cout << "[Runtime] Queue capacity: " << (queueCapacity == 0 ? std::string("unbounded") : std::to_string(queueCapacity)) << std::endl;

    // 时间 → 帧号转换 (用户友好: 指定时间范围而非帧号)
    int start_idx = static_cast<int>(start_sec * fps);
    int end_idx   = static_cast<int>(end_sec * fps);

    // ---- 4. 启动管线 (三段并发) ----
    auto t_total_start = std::chrono::high_resolution_clock::now();

    // 4a. Reader 线程 (IO 密集型, 1 个)
    auto t_reader_start = std::chrono::high_resolution_clock::now();
    std::thread reader(readerThread, std::ref(dec), std::ref(readQueue),
                       std::ref(stats));

    // 4b. Encryptor 线程池 (CPU 密集型, N 个)
    auto t_encrypt_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> encryptors;
    for (int i = 0; i < numThreads; ++i) {
        encryptors.emplace_back(encryptThread,
                                std::ref(readQueue), std::ref(writeQueue),
                                start_idx, end_idx, seed, i, std::ref(stats));
    }

    // 4c. Writer 线程 (IO 密集型, 1 个)
    auto t_writer_start = std::chrono::high_resolution_clock::now();
    std::thread writerT(writerThread, std::ref(writer), std::ref(writeQueue),
                        std::ref(stats));

    // ---- 5. 等待所有线程结束 (join) ----
    // 注意 join 的顺序: Reader → Encryptor → Writer
    // 原因: Reader 先结束才能让 Encryptor 看到 finished;
    //      所有 Encryptor 结束才能让 Writer 看到 finished;
    //      实际上 writeQueue.setFinished() 由主线程在 encryptor join 后调用

    reader.join();
    auto t_reader_end = std::chrono::high_resolution_clock::now();

    for (auto &t : encryptors) {
        t.join();
    }
    auto t_encrypt_end = std::chrono::high_resolution_clock::now();

    // 所有加密线程退出后 → 通知 writer "不会有新数据了"
    writeQueue.setFinished();
    writerT.join();
    auto t_writer_end = std::chrono::high_resolution_clock::now();

    // ---- 6. 计算耗时统计 ----
    stats.readerTimeMs = std::chrono::duration<double, std::milli>(
                             t_reader_end - t_reader_start).count();
    stats.encryptTimeMs = std::chrono::duration<double, std::milli>(
                              t_encrypt_end - t_encrypt_start).count();
    stats.writerTimeMs = std::chrono::duration<double, std::milli>(
                             t_writer_end - t_writer_start).count();
    double totalTimeMs = std::chrono::duration<double, std::milli>(
                             t_writer_end - t_total_start).count();

    // ---- 7. 计算吞吐量 ----
    int totalFrames = dec.getFrameCount();
    int encryptedFrames = stats.totalFramesEncrypted.load();

    // 整体吞吐量: 总帧数 / 总时间
    double throughputFps = (totalTimeMs > 0)
                           ? (totalFrames / totalTimeMs * 1000.0) : 0;

    // 加密吞吐量: 加密帧数 * 每帧大小 / 加密耗时
    // 每帧 = width * height * 3 字节 (BGR24)
    double mbPerFrame = width * height * 3.0 / (1024.0 * 1024.0);
    double encryptThroughput = (stats.encryptTimeMs > 0)
        ? (encryptedFrames * mbPerFrame / stats.encryptTimeMs * 1000.0) : 0;

    // ---- 8. 打印性能报告 ----
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n";
    std::cout << "========================================================\n";
    std::cout << "              Performance Report\n";
    std::cout << "========================================================\n";
    std::cout << "  Resolution:          " << width << "x" << height << "\n";
    std::cout << "  FPS:                 " << fps << "\n";
    std::cout << "  Total frames:        " << totalFrames << "\n";
    std::cout << "  Frames decoded:       " << stats.totalFramesDecoded.load() << "\n";
    std::cout << "  Frames encrypted:    " << encryptedFrames
              << "  (range: " << start_idx << " - " << end_idx << ")\n";
    std::cout << "  Frames written:       " << stats.totalFramesWritten.load() << "\n";
    std::cout << "  Encrypt threads:     " << numThreads << "\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "  Reader time:         " << stats.readerTimeMs  << " ms\n";
    std::cout << "  Encrypt time:        " << stats.encryptTimeMs << " ms"
              << "  (" << numThreads << " threads)\n";
    std::cout << "  Writer time:         " << stats.writerTimeMs  << " ms\n";
    std::cout << "  Total wall time:     " << totalTimeMs << " ms\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "  Throughput (overall):  " << throughputFps
              << " frames/s\n";
    std::cout << "  Throughput (encrypt):  " << encryptThroughput
              << " MB/s\n";
    std::cout << "--------------------------------------------------------\n";
    std::cout << "  readQueue  peak depth: " << stats.readQueuePeak.load()
              << " frames\n";
    std::cout << "  writeQueue peak depth: " << stats.writeQueuePeak.load()
              << " frames\n";
    std::cout << "========================================================\n\n";

    // ---- 9. 清理资源 ----
    dec.release();
    writer.release();

    return true;
}
