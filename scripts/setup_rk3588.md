# RK3588 (Orange Pi 5) 环境配置指南

## 概述

VideoChaosCipher 在 RK3588 上利用 VPU 硬件加速视频编解码。这需要：
- OpenCV 编译时启用 FFmpeg 后端
- FFmpeg 编译时集成 rkmpp (Rockchip Media Process Platform)

## 1. 确认 VPU 驱动已加载

```bash
# 检查 rkmpp 设备节点
ls /dev/mpp_service
# 或
ls /dev/rga

# 检查内核模块
lsmod | grep rk
```

## 2. 安装 Rockchip MPP 库

```bash
# Orange Pi 5 官方镜像通常已预装。如果没有：
sudo apt update
sudo apt install rockchip-mpp rockchip-mpp-dev
```

## 3. 安装 FFmpeg (带 rkmpp 支持)

```bash
# 方法 A：从源码编译 FFmpeg with rkmpp
git clone https://github.com/FFmpeg/FFmpeg.git
cd FFmpeg
./configure \
    --enable-rkmpp \
    --enable-encoder=h264_rkmpp \
    --enable-decoder=h264_rkmpp \
    --enable-encoder=hevc_rkmpp \
    --enable-decoder=hevc_rkmpp \
    --enable-libdrm \
    --enable-gpl \
    --enable-nonfree
make -j$(nproc)
sudo make install

# 方法 B：使用系统包管理器 (如果可用)
sudo apt install ffmpeg
# 检查 rkmpp 编解码器是否可用
ffmpeg -codecs | grep rkmpp
```

## 4. 编译 OpenCV (带 FFmpeg + rkmpp)

```bash
git clone https://github.com/opencv/opencv.git
cd opencv
mkdir build && cd build

cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DWITH_FFMPEG=ON \
    -DWITH_GSTREAMER=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DBUILD_opencv_python3=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_PERF_TESTS=OFF

make -j$(nproc)
sudo make install
```

## 5. 验证 VPU 加速是否生效

```bash
# 运行 VideoChaosCipher，观察 CPU 占用
./app input.mp4 output.mp4

# 同时在另一个终端监控：
htop
# 如果 VPU 正常工作，视频解码时 CPU 占用应很低（<5%），
# 主要 CPU 消耗应在加密计算线程上
```

## 6. 性能对比 (可选)

```bash
# 对比不同线程数的吞吐量
./app input.mp4 output.mp4 -t 1   # 单加密线程
./app input.mp4 output.mp4 -t 2   # 2 线程
./app input.mp4 output.mp4 -t 4   # 4 线程
./app input.mp4 output.mp4 -t 8   # 8 线程 (RK3588 全部核心)

# 查看 Benchmark 报告对比 Encrypt time 和 Throughput
```
