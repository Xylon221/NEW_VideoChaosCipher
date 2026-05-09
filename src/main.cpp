#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdlib>
#include "/home/orangepi/Work/VideoChaosCipher/include/encryptor.h"
#include "/home/orangepi/Work/VideoChaosCipher/include/SafeQueue.h"

struct FrameData
{
    cv::Mat frame;
    int frame_index;
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

void readerThread(cv::VideoCapture &cap, SafeQueue<FrameData> &readQueue) {
    std::cout << "Read started." << std::endl;
    int index = 0;
   FrameData data;
    while (true) {
        FrameData data;
        if( !cap.read(data.frame)){
            break; 
        }
        data.frame_index = index++;
        readQueue.push(data);
    }
    readQueue.setFinished();
    std::cout << "Read finished, totally read " << index << " frame" << std::endl;
}

// Encrypt Thread
// get frame from readQueue, encrypt them, then push them into writeQueue
// seed: 混沌加密种子，同一 seed 加密/解密结果一致
void encryptThread(SafeQueue<FrameData> &readQueue, SafeQueue<FrameData> &writeQueue,
                  int start_idx, int end_idx, float seed) {
    std::cout << "Encrypt video started." << std::endl;
    FrameData data;
    int index = 0;
    while (readQueue.pop(data)) {
        if (data.frame_index >= start_idx && data.frame_index <= end_idx) {
            encryptFrame(data.frame, seed);
            index++;
        }
        writeQueue.push(data);

    }
    std::cout << "Encrypt finished, totally encrypt " << index << " frame" << std::endl;
}

// Video writing thread
// It gets the frames from the writeQueue and writes them to VideoWriter
void writerThread(cv::VideoWriter &writer, SafeQueue<FrameData> &writeQueue) {
    std::cout << "Read started." << std::endl;
    FrameData data;
    int index = 0;
    while (writeQueue.pop(data)) {
        writer.write(data.frame);
        index++;
    }
    std::cout << "Write finished, totally write " << index << " frame" << std::endl;
}

// start_sec: 开始加密的时间（秒）
// end_sec:   结束加密的时间（秒）
// seed:      混沌加密种子
bool processVideo(const std::string &input_path, const std::string &output_path,
                  float start_sec, float end_sec, float seed) {
    cv::VideoCapture cap(input_path);
    if (!cap.isOpened()) {
        std::cerr << "Failed to open the input video:" << input_path << std::endl;
        return false;
    }
    
    printVideoInfo(cap);
    
    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
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
    // 根据时间参数计算帧索引范围
    int start_idx = static_cast<int>(start_sec * fps);
    int end_idx = static_cast<int>(end_sec * fps);
    
    // 启动所有线程
    std::thread reader(readerThread, std::ref(cap), std::ref(readQueue));
    std::thread encryptor(encryptThread, std::ref(readQueue), std::ref(writeQueue),
                          start_idx, end_idx, seed);
    std::thread writerT(writerThread, std::ref(writer), std::ref(writeQueue));
    
    // 等待并同步
    reader.join();      // 等待读取完成
    encryptor.join();   // 等待加密完成
    writeQueue.setFinished(); // 标记队列结束
    writerT.join();     // 等待写入完成
    
    cap.release();
    writer.release();
    
    return true;
}

int main(int argc, char* argv[]) {
    std::cout << "Program start." << std::endl;
    std::cout << "OpenCV Version: " << CV_VERSION << std::endl;

    // ---------- 解析命令行参数 ----------
    // 用法: ./app <输入视频> <输出视频> [开始秒数] [结束秒数] [种子值]
    if (argc < 3) {
        std::cout << "用法: " << argv[0]
                  << " <输入视频> <输出视频> [开始秒数] [结束秒数] [种子值]"
                  << std::endl;
        std::cout << "示例: " << argv[0]
                  << " input.mp4 output.mp4 2.0 4.0 0.5"
                  << std::endl;
        return -1;
    }

    std::string input_video  = argv[1];
    std::string output_video = argv[2];

    // 可选参数，未提供时使用默认值
    float start_sec = (argc >= 4) ? std::atof(argv[3]) : 2.0f;
    float end_sec   = (argc >= 5) ? std::atof(argv[4]) : 4.0f;
    float seed      = (argc >= 6) ? std::atof(argv[5]) : 0.5f;

    std::cout << "输入视频: " << input_video << std::endl;
    std::cout << "输出视频: " << output_video << std::endl;
    std::cout << "加密范围: " << start_sec << "s ~ " << end_sec << "s" << std::endl;
    std::cout << "加密种子: " << seed << std::endl;

    // ---------- 处理视频 ----------
    auto start = std::chrono::high_resolution_clock::now();

    if (!processVideo(input_video, output_video, start_sec, end_sec, seed)) {
        std::cerr << "Video process failed." << std::endl;
        return -1;
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto time_span_s = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time span: " << time_span_s.count() << "ms\n";

    std::cout << "Video process finished." << std::endl;

    return 0;
}