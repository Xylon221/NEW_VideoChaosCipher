#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>
#include <atomic>
#include <map>
#include <iomanip>
#include "/home/orangepi/Work/VideoChaosCipher/include/encryptor.h"
#include "/home/orangepi/Work/VideoChaosCipher/include/SafeQueue.h"

struct FrameData
{
    cv::Mat frame;
    int frame_index;
};

// 性能统计数据结构，各线程共享，通过 atomic 保证线程安全
struct BenchStats
{
    std::atomic<int> readQueueDepth{0};
    std::atomic<int> readQueuePeak{0};
    std::atomic<int> writeQueueDepth{0};
    std::atomic<int> writeQueuePeak{0};
    std::atomic<int> totalFramesEncrypted{0};

    // 各阶段耗时（毫秒）
    double readerTimeMs  = 0;
    double encryptTimeMs = 0;
    double writerTimeMs  = 0;
};

void printVideoInfo(cv::VideoCapture &cap) {
    if (!cap.isOpened()) {
        std::cout << "错误: 视频未打开" << std::endl;
        return;
    }
    
    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    int framecount = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    
    std::cout << "Original Video Intro:" << std::endl;
    std::cout << " Resolution: " << width << "x" << height << std::endl;
    std::cout << " FPS: " << fps  << std::endl;
    std::cout << " Total frames: " << framecount << std::endl;
}

bool openVideoFile(const std::string &path, cv::VideoCapture &cap) {
    cap.open(path);
    if (!cap.isOpened()) {
        std::cout << "ERROR: Failed to open Video." << std::endl;
        std::cout << "PATH: " << path << std::endl;
        return false;
    }
    return true;
}

void readerThread(cv::VideoCapture &cap, SafeQueue<FrameData> &readQueue,
                   BenchStats &stats) {
    std::cout << "Read started." << std::endl;
    int index = 0;
    while (true) {
        FrameData data;
        if (!cap.read(data.frame)) {
            break;
        }
        data.frame_index = index++;

        // 统计队列深度峰值（入队前递增，跨线程原子操作）
        int d = ++stats.readQueueDepth;
        if (d > stats.readQueuePeak.load()) {
            stats.readQueuePeak.store(d);
        }

        readQueue.push(data);
    }
    readQueue.setFinished();
    std::cout << "Read finished, totally read " << index << " frame" << std::endl;
}

// Encrypt Thread (可多个实例并行)
// 从 readQueue 取帧，在加密范围内则调用 encryptFrame，推入 writeQueue
void encryptThread(SafeQueue<FrameData> &readQueue, SafeQueue<FrameData> &writeQueue,
                   int start_idx, int end_idx, float seed, int threadId,
                   BenchStats &stats) {
    FrameData data;
    int localEncrypted = 0;   // 本线程加密帧计数
    while (readQueue.pop(data)) {
        // 从读队列取出，深度减 1
        --stats.readQueueDepth;

        if (data.frame_index >= start_idx && data.frame_index <= end_idx) {
            encryptFrame(data.frame, seed);
            localEncrypted++;
        }

        // 推入写队列前统计深度峰值
        int d = ++stats.writeQueueDepth;
        if (d > stats.writeQueuePeak.load()) {
            stats.writeQueuePeak.store(d);
        }

        writeQueue.push(data);
    }

    // 累加到全局加密计数
    stats.totalFramesEncrypted += localEncrypted;
    std::cout << "Encryptor " << threadId << " finished, encrypted "
              << localEncrypted << " frames" << std::endl;
}

// Video writing thread (带乱序重排缓冲)
// 多加密线程可能导致帧乱序到达，使用 std::map 按 frame_index 排序后顺序写入
void writerThread(cv::VideoWriter &writer, SafeQueue<FrameData> &writeQueue,
                  BenchStats &stats) {
    std::cout << "Write started." << std::endl;

    FrameData data;
    int nextToWrite = 0;                     // 下一个期望写入的帧序号
    std::map<int, FrameData> reorderBuffer;  // 乱序帧暂存，按序号排序
    int index = 0;

    while (writeQueue.pop(data)) {
        --stats.writeQueueDepth;

        // 将帧放入重排缓冲（std::map 自动按 key 排序）
        reorderBuffer[data.frame_index] = std::move(data);

        // 尽可能写出所有已连续到达的帧
        while (!reorderBuffer.empty() &&
               reorderBuffer.begin()->first == nextToWrite) {
            writer.write(reorderBuffer.begin()->second.frame);
            reorderBuffer.erase(reorderBuffer.begin());
            nextToWrite++;
            index++;
        }
    }

    // writeQueue 已标记结束，写出缓冲中剩余的帧
    for (auto &pair : reorderBuffer) {
        writer.write(pair.second.frame);
        index++;
    }

    std::cout << "Write finished, totally write " << index << " frame" << std::endl;
}

// start_sec: 开始加密的时间（秒）
// end_sec:   结束加密的时间（秒）
// seed:      混沌加密种子
// numThreads: 加密线程数
bool processVideo(const std::string &input_path, const std::string &output_path,
                  float start_sec, float end_sec, float seed, int numThreads) {
    cv::VideoCapture cap(input_path);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open the input video:" << input_path << std::endl;
        return false;
    }

    printVideoInfo(cap);

    int width  = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS);

    if (fps <= 0) {
        std::cerr << "ERROR: Invalid FPS" << std::endl;
        return false;
    }

    cv::VideoWriter writer(
        output_path,
        cv::VideoWriter::fourcc('a', 'v', 'c', '1'),
        fps,
        cv::Size(width, height)
    );

    if (!writer.isOpened()) {
        std::cerr << "Failed to open the output video:" << output_path << std::endl;
        return false;
    }

    SafeQueue<FrameData> readQueue, writeQueue;
    BenchStats stats;
    int start_idx = static_cast<int>(start_sec * fps);
    int end_idx   = static_cast<int>(end_sec * fps);

    // ---------- 计时起点 ----------
    auto t_total_start = std::chrono::high_resolution_clock::now();

    // --- 启动 reader ---
    auto t_reader_start = std::chrono::high_resolution_clock::now();
    std::thread reader(readerThread, std::ref(cap), std::ref(readQueue),
                       std::ref(stats));

    // --- 启动 N 个 encryptor ---
    auto t_encrypt_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> encryptors;
    for (int i = 0; i < numThreads; ++i) {
        encryptors.emplace_back(encryptThread,
                                std::ref(readQueue), std::ref(writeQueue),
                                start_idx, end_idx, seed, i, std::ref(stats));
    }

    // --- 启动 writer ---
    auto t_writer_start = std::chrono::high_resolution_clock::now();
    std::thread writerT(writerThread, std::ref(writer), std::ref(writeQueue),
                        std::ref(stats));

    // ---------- 同步 ----------
    reader.join();
    auto t_reader_end = std::chrono::high_resolution_clock::now();

    for (auto &t : encryptors) {
        t.join();
    }
    auto t_encrypt_end = std::chrono::high_resolution_clock::now();

    writeQueue.setFinished();
    writerT.join();
    auto t_writer_end = std::chrono::high_resolution_clock::now();

    // ---------- 记录各阶段耗时 ----------
    stats.readerTimeMs = std::chrono::duration<double, std::milli>(
                             t_reader_end - t_reader_start).count();
    stats.encryptTimeMs = std::chrono::duration<double, std::milli>(
                              t_encrypt_end - t_encrypt_start).count();
    stats.writerTimeMs = std::chrono::duration<double, std::milli>(
                             t_writer_end - t_writer_start).count();
    double totalTimeMs = std::chrono::duration<double, std::milli>(
                             t_writer_end - t_total_start).count();

    // ---------- 输出性能报告 ----------
    int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    int encryptedFrames = stats.totalFramesEncrypted.load();
    double throughputFps = (totalTimeMs > 0)
                           ? (totalFrames / totalTimeMs * 1000.0) : 0;

    // 估算加密吞吐量 (MB/s): 帧宽×高×3 字节 × 加密帧数 / 时间
    double mbPerFrame = width * height * 3.0 / (1024.0 * 1024.0);
    double encryptThroughput = (stats.encryptTimeMs > 0)
        ? (encryptedFrames * mbPerFrame / stats.encryptTimeMs * 1000.0) : 0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "\n";
    std::cout << "========================================================\n";
    std::cout << "              Performance Report\n";
    std::cout << "========================================================\n";
    std::cout << "  Resolution:          " << width << "x" << height << "\n";
    std::cout << "  FPS:                 " << fps << "\n";
    std::cout << "  Total frames:        " << totalFrames << "\n";
    std::cout << "  Frames encrypted:    " << encryptedFrames
              << "  (range: " << start_idx << " - " << end_idx << ")\n";
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

    cap.release();
    writer.release();

    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "Program start." << std::endl;
    std::cout << "OpenCV Version: " << CV_VERSION << std::endl;

    // ---------- 解析命令行参数 ----------
    // 用法: ./app <输入视频> <输出视频> [开始秒数] [结束秒数] [种子值] [-t 线程数]
    if (argc < 3) {
        std::cout << "用法: " << argv[0]
                  << " <输入视频> <输出视频> [开始秒数] [结束秒数] [种子值] [-t 线程数]"
                  << std::endl;
        std::cout << "示例: " << argv[0]
                  << " input.mp4 output.mp4 2.0 4.0 0.5 -t 4"
                  << std::endl;
        return -1;
    }

    std::string input_video  = argv[1];
    std::string output_video = argv[2];

    // 分离位置参数和 -t 标志
    std::vector<std::string> positional;
    int numThreads = 1;
    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        if (arg == "-t" && i + 1 < argc) {
            numThreads = std::atoi(argv[++i]);
        } else {
            positional.push_back(arg);
        }
    }

    float start_sec = (positional.size() > 0)
                      ? std::atof(positional[0].c_str()) : 2.0f;
    float end_sec   = (positional.size() > 1)
                      ? std::atof(positional[1].c_str()) : 4.0f;
    float seed      = (positional.size() > 2)
                      ? std::atof(positional[2].c_str()) : 0.5f;

    if (numThreads < 1) numThreads = 1;

    std::cout << "输入视频:   " << input_video  << std::endl;
    std::cout << "输出视频:   " << output_video << std::endl;
    std::cout << "加密范围:   " << start_sec << "s ~ " << end_sec << "s" << std::endl;
    std::cout << "加密种子:   " << seed       << std::endl;
    std::cout << "加密线程数: " << numThreads << std::endl;

    // ---------- 处理视频 ----------
    auto start = std::chrono::high_resolution_clock::now();

    if (!processVideo(input_video, output_video, start_sec, end_sec,
                      seed, numThreads)) {
        std::cerr << "Video process failed." << std::endl;
        return -1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time_span_s = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time span: " << time_span_s.count() << "ms\n";

    std::cout << "Video process finished." << std::endl;

    return 0;
}