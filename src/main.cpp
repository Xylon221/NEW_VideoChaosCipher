#include <iostream>
#include <opencv2/opencv.hpp>
#include <random>
#include <chrono>  
struct FrameData
{
    cv::Mat frame;
    int frame_index;
};

void printVideoInfo(cv::VideoCapture& cap) 
{
    if (!cap.isOpened()) 
    {
        std::cout << "错误: 视频未打开" << std::endl;
        return;
    }

    int width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);
    int framecount = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));


    std::cout << "视频信息:" << std::endl;
    std::cout << "  分辨率: " << width << "x" << height << std::endl;
    std::cout << "  帧率: " << fps << " FPS" << std::endl;
    std::cout << "  总帧数: " << framecount << std::endl;
}

void testOpenCV() 
{
    std::cout << "OpenCV版本: " << CV_VERSION << std::endl;
    
    cv::Mat testImage(100, 100, CV_8UC3, cv::Scalar(0, 0, 255));
    std::cout << "测试图像尺寸: " << testImage.cols << "x" << testImage.rows << std::endl;
}

bool openVideoFile(const std::string& path, cv::VideoCapture& cap) 
{
    cap.open(path);
    if (!cap.isOpened()) 
    {
        std::cout << "错误: 无法打开视频文件" << std::endl;
        std::cout << "路径: " << path << std::endl;
        return false;
    }
    return true;
}


void encryptFrame(cv::Mat& frame)
{
    CV_Assert(frame.type() == CV_8UC3);

    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<uint8_t> dist(0, 255);

    for (int y = 0; y < frame.rows; ++y)
    {
        uint8_t* row = frame.ptr<uint8_t>(y);
        for (int x = 0; x < frame.cols * 3; ++x)
        {
            row[x] ^= dist(rng);
        }
    }
}



bool processVideo(const std::string& input_path, const std::string& output_path){

    cv::VideoCapture cap(input_path);
    if (!cap.isOpened())
    {
        std::cerr << "无法打开输入视频: " << input_path << std::endl;
        return false;
    }

    printVideoInfo(cap);

    int width = cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);

    cv::VideoWriter writer(
        output_path,
        cv::VideoWriter::fourcc('a','v','c','1'), // AVI 常用
        fps,
        cv::Size(width, height)
    );

    if (!writer.isOpened())
    {
        std::cerr << "无法创建输出视频: " << output_path << std::endl;
        return false;
    }

    int index = 0;
    FrameData data;

    int start_idx = static_cast<int>(2.0 * fps);
    int end_idx   = static_cast<int>(4.0 * fps);
    auto start = std::chrono::high_resolution_clock::now();
    while (true)
    {
        if (!cap.read(data.frame))
            break;
        if (index >= start_idx && index < end_idx)
        {
            // ===== 2s–4s 区间：加密 =====
            encryptFrame(data.frame);
        }
        data.frame_index = index++;
        writer.write(data.frame);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = end - start;
    std::cout << "加密时间: " << duration.count()* 1000.0 << "ms\n";


    std::cout << "已处理帧数: " << index << std::endl;

    cap.release();
    writer.release();
    return true;
}


int main()
{
    std::cout << "程序启动" << std::endl;
    std::cout << "OpenCV 版本: " << CV_VERSION << std::endl;

    std::string input_video  = "/home/orangepi/Work/CHAPT1/test_video.mp4";
    std::string output_video = "/home/orangepi/Work/VideoChaosCipher/output_video.mp4";

    auto start = std::chrono::high_resolution_clock::now();
    if (!processVideo(input_video, output_video))
    {
        std::cerr << "视频转换失败" << std::endl;
        return -1;
    }
    auto end = std::chrono::high_resolution_clock::now();
    
    auto time_span_s = std::chrono::duration_cast<std::chrono::milliseconds> (end - start);
    std::cout << "Time span: " << time_span_s.count() * 1000.0 << "ms\n";
    std::cout << "视频转换完成" << std::endl;
    return 0; 
}