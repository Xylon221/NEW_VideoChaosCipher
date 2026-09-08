# RK3588 上板配置与验证指南

> 目标：在 Orange Pi 5/RK3588 上验证 VideoChaosCipher 是否走到了 VPU 硬件编解码路径，并完成摄像头或视频文件端到端测试。注意：代码会硬件优先尝试，但是否真正启用 VPU 取决于系统镜像、驱动、FFmpeg 编译选项和输入格式。

## 1. 检查硬件设备

```bash
ls -l /dev/mpp_service /dev/rga 2>/dev/null
ls -l /dev/video* 2>/dev/null
```

常见设备：

- `/dev/mpp_service`：Rockchip MPP 服务。
- `/dev/rga`：图像加速/格式转换相关设备。
- `/dev/video*`：摄像头或 V4L2 M2M 设备节点。

不同系统镜像暴露的节点名称可能不同，不能只靠节点判断 VPU 一定可用。

## 2. 检查 FFmpeg 编解码器

项目提供脚本：

```bash
cd /home/xylon/workspace/VideoChaosCipher
chmod +x scripts/inspect_codecs.sh
./scripts/inspect_codecs.sh
```

也可以手动检查：

```bash
ffmpeg -hide_banner -codecs | grep -E 'rkmpp|v4l2m2m|h264|hevc'
ffmpeg -hide_banner -decoders | grep -E 'rkmpp|v4l2m2m'
ffmpeg -hide_banner -encoders | grep -E 'rkmpp|v4l2m2m'
```

如果没有 `rkmpp` 或 `v4l2m2m`，程序仍可运行，但会进入软件编解码回退路径。

## 3. 安装依赖

Ubuntu/Debian 系统可先安装通用依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config git \
    libopencv-dev libavcodec-dev libavformat-dev libavutil-dev libswscale-dev \
    libgtest-dev ffmpeg
```

板端是否具备 rkmpp 支持取决于镜像仓库提供的 FFmpeg。若 apt 版本没有硬件编解码器，需要使用厂商镜像、第三方 RK3588 多媒体包，或自行编译 FFmpeg + Rockchip MPP。

## 4. 构建项目

```bash
cd /home/xylon/workspace/VideoChaosCipher
cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build-wsl -j$(nproc)
```

运行测试：

```bash
cd build-wsl
ctest --output-on-failure
```

当前预期是 15 个测试通过。单元测试不依赖真实 VPU，可以先验证核心逻辑。

## 5. 视频文件测试

优先用 H.264/H.265 文件验证硬解码，因为摄像头默认可能输出 MJPEG/YUYV，未必触发 H.264/H.265 硬解。

```bash
./build-wsl/video_encryptor input.mp4 output.mp4 0 5 -t 4 -q 16
```

观察日志：

```text
[VPUDecoder] Opened: xxx (HW=yes/no)
[VPUEncoder] Opened: xxx (HW=yes/no)
```

`HW=yes` 表示对应阶段命中硬件编解码器；`HW=no` 表示软件回退。

## 6. 摄像头测试

先确认摄像头格式：

```bash
v4l2-ctl --list-devices
v4l2-ctl -d /dev/video0 --list-formats-ext
```

运行：

```bash
./build-wsl/video_encryptor /dev/video0 camera_out.mp4 0 10 -t 4 -q 8
```

如果摄像头输出 MJPEG 或 YUYV，日志不一定出现 H.264/H.265 硬解，这是采集格式决定的。要验证 H.264/H.265 VPU 解码，使用对应编码的视频文件更直接。

## 7. 性能记录建议

建议记录四组数据：

| 数据 | 命令或方式 |
| --- | --- |
| 实际编解码器 | 程序日志中的 `Opened: ... (HW=yes/no)` |
| CPU 占用 | `top`、`htop`、`pidstat -u -p <pid> 1` |
| 吞吐 | 程序输出 FPS 或总耗时/帧数 |
| 队列峰值 | 程序统计中的 queue peak |

至少测试：

```bash
./build-wsl/video_encryptor input.mp4 out_t1.mp4 0 5 -t 1 -q 16
./build-wsl/video_encryptor input.mp4 out_t4.mp4 0 5 -t 4 -q 16
./build-wsl/video_encryptor input.mp4 out_q8.mp4 0 5 -t 4 -q 8
```

## 8. 常见问题

### 只看到 HW=no，是否说明代码失败？

不一定。`HW=no` 说明当前环境没有成功打开硬件编解码器，软件回退正常工作。需要继续检查 FFmpeg 编译选项、驱动节点、输入编码格式和权限。

### 有 `/dev/video0` 是否等于能用 VPU？

不等于。`/dev/video0` 可能只是 USB 摄像头节点。VPU 是否可用要看 FFmpeg 是否有硬件编解码器、程序是否打开成功，以及运行日志是否显示 `HW=yes`。

### 为什么输出视频不能逐像素解密？

如果输出使用 H.264/H.265 有损编码，像素会被压缩器改动。XOR 算法本身可逆，但有损编码会破坏逐像素可逆条件。需要严格可逆时，改用无损编码或码流/文件层标准加密。
