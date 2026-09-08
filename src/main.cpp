#include <iostream>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <thread>
#include <string>
#include "pipeline.h"

static void printUsage(const char *prog) {
    std::cout
        << "Usage: " << prog << " <input> <output> [start_sec] [end_sec] [seed] [options]\n"
        << "\nOptions:\n"
        << "  -t, --threads N      CPU encryptor thread count; 0 = hardware_concurrency\n"
        << "  -q, --queue N        Bounded queue capacity per stage; 0 = unbounded (default: 8)\n"
        << "  -h, --help           Show this help\n"
        << "\nExample:\n"
        << "  " << prog << " input.mp4 encrypted.mp4 2.0 4.0 0.5 -t 4 -q 8\n"
        << "  " << prog << " encrypted.mp4 restored.mp4 2.0 4.0 0.5 -t 4 -q 8\n";
}

static int parseInt(const std::string &s, int fallback) {
    char *end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    return (end && *end == '\0') ? static_cast<int>(v) : fallback;
}

int main(int argc, char* argv[]) {
    std::cout << "Program start." << std::endl;
    std::cout << "OpenCV Version: " << CV_VERSION << std::endl;

    if (argc < 2 || std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help") {
        printUsage(argv[0]);
        return argc < 2 ? -1 : 0;
    }
    if (argc < 3) {
        printUsage(argv[0]);
        return -1;
    }

    std::string input_video  = argv[1];
    std::string output_video = argv[2];

    std::vector<std::string> positional;
    int numThreads = 1;
    std::size_t queueCapacity = 8;

    for (int i = 3; i < argc; ++i) {
        std::string arg(argv[i]);
        if ((arg == "-t" || arg == "--threads") && i + 1 < argc) {
            numThreads = parseInt(argv[++i], 1);
        } else if ((arg == "-q" || arg == "--queue") && i + 1 < argc) {
            int parsed = parseInt(argv[++i], 8);
            queueCapacity = parsed < 0 ? 8 : static_cast<std::size_t>(parsed);
        } else if (arg == "-h" || arg == "--help") {
            printUsage(argv[0]);
            return 0;
        } else {
            positional.push_back(arg);
        }
    }

    float start_sec = (positional.size() > 0) ? std::atof(positional[0].c_str()) : 2.0f;
    float end_sec   = (positional.size() > 1) ? std::atof(positional[1].c_str()) : 4.0f;
    float seed      = (positional.size() > 2) ? std::atof(positional[2].c_str()) : 0.5f;

    if (numThreads == 0) {
        unsigned hw = std::thread::hardware_concurrency();
        numThreads = hw == 0 ? 1 : static_cast<int>(hw);
    }
    if (numThreads < 1) numThreads = 1;
    if (seed <= 0.0f || seed >= 1.0f) {
        std::cerr << "Seed must be in (0, 1); got " << seed << std::endl;
        return -1;
    }
    if (end_sec < start_sec) {
        std::cerr << "end_sec must be >= start_sec" << std::endl;
        return -1;
    }

    std::cout << "Input:   " << input_video  << std::endl;
    std::cout << "Output:  " << output_video << std::endl;
    std::cout << "Range:   " << start_sec << "s ~ " << end_sec << "s" << std::endl;
    std::cout << "Seed:    " << seed       << std::endl;
    std::cout << "Threads: " << numThreads << std::endl;
    std::cout << "Queue:   " << (queueCapacity == 0 ? std::string("unbounded") : std::to_string(queueCapacity)) << std::endl;

    auto start = std::chrono::high_resolution_clock::now();
    if (!processVideo(input_video, output_video, start_sec, end_sec, seed, numThreads, queueCapacity)) {
        std::cerr << "Video process failed." << std::endl;
        return -1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time_span_s = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time span: " << time_span_s.count() << "ms\n";
    std::cout << "Video process finished." << std::endl;
    return 0;
}
