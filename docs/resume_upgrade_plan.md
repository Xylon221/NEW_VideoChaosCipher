# VideoChaosCipher 秋招项目升级说明

## 项目定位

面向边缘视频采集设备在传输和存储过程中的隐私保护需求，构建基于 RK3588 异构计算平台的视频流轻量级加密系统。系统将 H.264/H.265 编解码优先交给 VPU，CPU 侧通过多线程流水线执行帧级混沌加密，降低端到端处理延迟并保留可观测的性能指标。

## 当前升级点

- 编解码层：FFmpeg 解码器按 `h264_rkmpp/hevc_rkmpp -> *_v4l2m2m -> 软件解码` 的顺序选择，编码器按 `h264_rkmpp -> h264_v4l2m2m -> libx264/软件 H.264` 的顺序选择。
- 可观测性：运行时打印实际打开的 decoder/encoder 名称与 `HW=yes/no`，开发板上可直接判断是否真正走 VPU。
- Pipeline：Reader、Encryptor、Writer 三阶段并行，队列升级为有界阻塞队列，避免高清视频帧积压导致内存过高。
- 参数化：新增 `-q/--queue` 控制队列容量，`-t/--threads 0` 自动使用 CPU 硬件并发数。
- 验证：补充有界队列单元测试；`scripts/run_benchmark.sh` 可在开发板上生成 benchmark 日志和报告。

## 开发板验证命令

```bash
cd ~/workspace/VideoChaosCipher
./scripts/inspect_codecs.sh
./scripts/run_benchmark.sh /path/to/input.mp4
```

重点看日志：

```text
[VPUDecoder] Opened: h264_rkmpp (HW=yes, ...)
[VPUEncoder] Opened: h264_rkmpp (HW=yes)
[Runtime] Queue capacity: 8
```

如果显示 `HW=no`，说明当前开发板 FFmpeg 没有启用对应硬件 codec，或硬件 codec 打开失败并回退到了软件实现。

## 简历表述

- 基于 C++/FFmpeg/OpenCV 设计边缘视频加密系统，接入 RKMPP/V4L2 M2M，实现 H.264/H.265 硬件编解码优先与软件 fallback。
- 设计 Reader-Worker-Writer 三阶段并行流水线，使用线程安全有界阻塞队列和帧序重排机制，实现解码、加密、编码并发处理。
- 优化运行时可观测性，统计各阶段耗时、队列峰值、加密帧数和整体吞吐，支持 RK3588 开发板 benchmark 对比。


## 摄像头实测

开发板接 USB/UVC 摄像头后可先确认设备：

```bash
./scripts/inspect_codecs.sh
ls -l /dev/video*
```

如果摄像头暴露为 `/dev/video0`，可以直接运行：

```bash
./build-wsl/app /dev/video0 camera_encrypted.mp4 0 10 0.5 -t 4 -q 8
```

当前程序会对 `/dev/video*` 使用 FFmpeg v4l2 输入。不同摄像头默认格式不同，若设备默认输出 MJPEG/YUYV，日志里的 decoder 可能不是 `h264_rkmpp`；这属于采集格式问题。若要验证 VPU H.264/H.265 解码，请使用 H.264/H.265 视频文件或配置摄像头输出对应压缩格式。

## 加解密边界

像素级 XOR 加密本身是可逆的，但如果加密帧随后经过有损 H.264/H.265 编码，像素值会被压缩器改变，因此再次 XOR 不能保证逐像素还原。工程上建议区分两种模式：

- 实时隐私加扰模式：VPU 有损编码，目标是让未授权方看不清画面，适合实时演示与边缘传输。
- 可逆归档模式：使用无损编码或在编码后的比特流/文件层做标准加密，目标是授权后完整恢复原始视频。

简历表述应强调“边缘端实时视频加扰/轻量级加密原型”，避免宣称达到工业级密码学安全或有损编码后的无损恢复。
