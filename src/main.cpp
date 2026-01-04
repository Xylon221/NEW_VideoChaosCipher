#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <chrono>
#include <thread>

#include "/home/orangepi/Work/VideoChaosCipher/include/SafeQueue.h"

struct FrameData
{
    cv::Mat frame;
    int frame_index;
    
    // 添加拷贝构造函数，确保深度拷贝
    FrameData() = default;
    FrameData(const FrameData& other) 
        : frame_index(other.frame_index)
    {
        other.frame.copyTo(frame);  // 深度拷贝
    }
    
    FrameData& operator=(const FrameData& other) {
        if (this != &other) {
            frame_index = other.frame_index;
            other.frame.copyTo(frame);  // 深度拷贝
        }
        return *this;
    }
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
    
    std::cout << "视频信息:" << std::endl;
    std::cout << " 分辨率: " << width << "x" << height << std::endl;
    std::cout << " 帧率: " << fps << " FPS" << std::endl;
    std::cout << " 总帧数: " << framecount << std::endl;
}

bool openVideoFile(const std::string &path, cv::VideoCapture &cap) {
    cap.open(path);
    if (!cap.isOpened()) {
        std::cout << "错误: 无法打开视频文件" << std::endl;
        std::cout << "路径: " << path << std::endl;
        return false;
    }
    return true;
}

void encryptFrame(cv::Mat &frame) {
    CV_Assert(frame.type() == CV_8UC3);
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<uint8_t> dist(0, 255);
    
    for (int y = 0; y < frame.rows; ++y) {
        uint8_t *row = frame.ptr<uint8_t>(y);
        for (int x = 0; x < frame.cols * 3; ++x) {
            row[x] ^= dist(rng);
        }
    }
}

// Video reading thread
// It reads frames from cap, and pushes them to the readQueue.
void readerThread(cv::VideoCapture &cap, SafeQueue<FrameData> &readQueue) {
    FrameData data;
    int index = 0;
    
    while (cap.read(data.frame)) {
        data.frame_index = index++;
        readQueue.push(data);
    }
    readQueue.setFinished();
}

// Encrypt Thread
// get frame from readQueue, encrypt them, then push them into writeQueue
void encryptThread(SafeQueue<FrameData> &readQueue, SafeQueue<FrameData> &writeQueue, 
                  int start_idx, int end_idx) {
    FrameData data;
    
    while (readQueue.pop(data)) {
        if (data.frame_index >= start_idx && data.frame_index <= end_idx) {
            encryptFrame(data.frame);
        }
        writeQueue.push(data);
    }
}

// Video writing thread
// It gets the frames from the writeQueue and writes them to VideoWriter
void writerThread(cv::VideoWriter &writer, SafeQueue<FrameData> &writeQueue) {
    FrameData data;
    
    while (writeQueue.pop(data)) {
        writer.write(data.frame);
    }
}

bool processVideo(const std::string &input_path, const std::string &output_path) {
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
        std::cerr << "错误: 无效帧率" << std::endl;
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
    int start_idx = static_cast<int>(2.0 * fps);
    int end_idx = static_cast<int>(4.0 * fps);
    
    // 启动所有线程
    std::thread reader(readerThread, std::ref(cap), std::ref(readQueue));
    std::thread encryptor(encryptThread, std::ref(readQueue), std::ref(writeQueue), 
                          start_idx, end_idx);
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

int main() {
    std::cout << "程序启动" << std::endl;
    std::cout << "OpenCV 版本: " << CV_VERSION << std::endl;
    
    std::string input_video = "/home/orangepi/Work/CHAPT1/test_video.mp4";
    std::string output_video = "/home/orangepi/Work/VideoChaosCipher/output_video.mp4";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    if (!processVideo(input_video, output_video)) {
        std::cerr << "视频转换失败" << std::endl;
        return -1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto time_span_s = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time span: " << time_span_s.count() << "ms\n";
    
    std::cout << "视频转换完成" << std::endl;
    
    return 0;
}