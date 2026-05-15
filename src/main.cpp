#include <iostream>
#include <opencv2/opencv.hpp>
#include <chrono>
#include <vector>
#include <cstdlib>
#include "pipeline.h"

int main(int argc, char* argv[]) {
    std::cout << "Program start." << std::endl;
    std::cout << "OpenCV Version: " << CV_VERSION << std::endl;

    if (argc < 3) {
        std::cout << "Usage: " << argv[0]
                  << " <input> <output> [start_sec] [end_sec] [seed] [-t threads]"
                  << std::endl;
        std::cout << "Example: " << argv[0]
                  << " input.mp4 output.mp4 2.0 4.0 0.5 -t 4"
                  << std::endl;
        return -1;
    }

    std::string input_video  = argv[1];
    std::string output_video = argv[2];

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

    std::cout << "Input:   " << input_video  << std::endl;
    std::cout << "Output:  " << output_video << std::endl;
    std::cout << "Range:   " << start_sec << "s ~ " << end_sec << "s" << std::endl;
    std::cout << "Seed:    " << seed       << std::endl;
    std::cout << "Threads: " << numThreads << std::endl;

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
