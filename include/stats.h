#ifndef STATS_H
#define STATS_H

#include <atomic>
#include <opencv2/opencv.hpp>

struct FrameData
{
    cv::Mat frame;
    int frame_index;
};

struct BenchStats
{
    std::atomic<int> readQueueDepth{0};
    std::atomic<int> readQueuePeak{0};
    std::atomic<int> writeQueueDepth{0};
    std::atomic<int> writeQueuePeak{0};
    std::atomic<int> totalFramesEncrypted{0};

    double readerTimeMs  = 0;
    double encryptTimeMs = 0;
    double writerTimeMs  = 0;
};

#endif
