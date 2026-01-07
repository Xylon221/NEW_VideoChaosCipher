#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <chrono>
#include <thread>
#include <vector>
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
void encryptThread(SafeQueue<FrameData> &readQueue, SafeQueue<FrameData> &writeQueue, 
                  int start_idx, int end_idx) {
    std::cout << "Encrypt video started." << std::endl;                
    FrameData data;
    int index = 0;
    while (readQueue.pop(data)) {
        if (data.frame_index >= start_idx && data.frame_index <= end_idx) {
            encryptFrame(data.frame);
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
    std::cout << "Program start." << std::endl;
    std::cout << "OpenCV Version: " << CV_VERSION << std::endl;
    
    std::string input_video = "/home/orangepi/Work/CHAPT1/test_video.mp4";
    std::string output_video = "/home/orangepi/Work/VideoChaosCipher/output_video.mp4";
    
    auto start = std::chrono::high_resolution_clock::now();
    
    if (!processVideo(input_video, output_video)) {
        std::cerr << "Video process failed." << std::endl;
        return -1;
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    auto time_span_s = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    std::cout << "Time span: " << time_span_s.count() << "ms\n";
    
    std::cout << "Video process finished." << std::endl;
    
    return 0;
}