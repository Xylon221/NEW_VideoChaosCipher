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

void encryptThread(SafeQueue<FrameData> &readQueue,
                   SafeQueue<FrameData> &writeQueue,
                   int start_idx, int end_idx, float seed, int threadId,
                   BenchStats &stats) {
    FrameData data;
    int localEncrypted = 0;
    while (readQueue.pop(data)) {
        --stats.readQueueDepth;

        if (data.frame_index >= start_idx && data.frame_index <= end_idx) {
            encryptFrame(data.frame, seed);
            localEncrypted++;
        }

        int d = ++stats.writeQueueDepth;
        if (d > stats.writeQueuePeak.load()) {
            stats.writeQueuePeak.store(d);
        }

        writeQueue.push(data);
    }

    stats.totalFramesEncrypted += localEncrypted;
    std::cout << "Encryptor " << threadId << " finished, encrypted "
              << localEncrypted << " frames" << std::endl;
}

bool processVideo(const std::string &input_path, const std::string &output_path,
                  float start_sec, float end_sec, float seed, int numThreads) {
    VPUDecoder dec;
    if (!dec.open(input_path)) {
        std::cerr << "Failed to open input video: " << input_path << std::endl;
        return false;
    }

    printVideoInfo(dec);

    int width  = dec.getWidth();
    int height = dec.getHeight();
    double fps = dec.getFPS();

    if (fps <= 0) {
        std::cerr << "ERROR: Invalid FPS" << std::endl;
        return false;
    }

    VPUEncoder writer;
    if (!writer.open(output_path, width, height, fps)) {
        std::cerr << "Failed to open output video: " << output_path << std::endl;
        return false;
    }

    SafeQueue<FrameData> readQueue, writeQueue;
    BenchStats stats;
    int start_idx = static_cast<int>(start_sec * fps);
    int end_idx   = static_cast<int>(end_sec * fps);

    auto t_total_start = std::chrono::high_resolution_clock::now();

    auto t_reader_start = std::chrono::high_resolution_clock::now();
    std::thread reader(readerThread, std::ref(dec), std::ref(readQueue),
                       std::ref(stats));

    auto t_encrypt_start = std::chrono::high_resolution_clock::now();
    std::vector<std::thread> encryptors;
    for (int i = 0; i < numThreads; ++i) {
        encryptors.emplace_back(encryptThread,
                                std::ref(readQueue), std::ref(writeQueue),
                                start_idx, end_idx, seed, i, std::ref(stats));
    }

    auto t_writer_start = std::chrono::high_resolution_clock::now();
    std::thread writerT(writerThread, std::ref(writer), std::ref(writeQueue),
                        std::ref(stats));

    reader.join();
    auto t_reader_end = std::chrono::high_resolution_clock::now();

    for (auto &t : encryptors) {
        t.join();
    }
    auto t_encrypt_end = std::chrono::high_resolution_clock::now();

    writeQueue.setFinished();
    writerT.join();
    auto t_writer_end = std::chrono::high_resolution_clock::now();

    stats.readerTimeMs = std::chrono::duration<double, std::milli>(
                             t_reader_end - t_reader_start).count();
    stats.encryptTimeMs = std::chrono::duration<double, std::milli>(
                              t_encrypt_end - t_encrypt_start).count();
    stats.writerTimeMs = std::chrono::duration<double, std::milli>(
                             t_writer_end - t_writer_start).count();
    double totalTimeMs = std::chrono::duration<double, std::milli>(
                             t_writer_end - t_total_start).count();

    int totalFrames = dec.getFrameCount();
    int encryptedFrames = stats.totalFramesEncrypted.load();
    double throughputFps = (totalTimeMs > 0)
                           ? (totalFrames / totalTimeMs * 1000.0) : 0;

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

    dec.release();
    writer.release();

    return true;
}
