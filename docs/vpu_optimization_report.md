# VPU 硬件加速优化报告

## 概述

将 VideoChaosCipher 视频 I/O 层从 OpenCV 的 `cv::VideoCapture` / `cv::VideoWriter`（底层 libx264 软件编解码）替换为 FFmpeg API 直调方案，尝试启用 RK3588 VPU 硬件加速（rkmpp），在实际硬件不可用时自动回退到 FFmpeg 软件编解码（ARM NEON + 多线程）。整体管线吞吐量提升约 **1.8 倍**。

- **分支**: `feature/vpu-ffmpeg-accel`
- **平台**: Orange Pi 5 (Rockchip RK3588)
- **日期**: 2026-05-14

---

## 平台环境

| 项目 | 信息 |
|------|------|
| 开发板 | Orange Pi 5 |
| SoC | Rockchip RK3588 (4×A76 + 4×A55) |
| 内存 | 7.8 GB |
| 内核 | Linux 5.10.160-rockchip-rk3588 |
| 编译器 | g++ 11.4.0 (ARMv8) |
| OpenCV | 4.5.4 |
| FFmpeg | 4.4.2 (编译了 rkmpp / v4l2m2m 支持) |
| MPP | librockchip_mpp.so.1 (已安装) |
| VPU 设备 | `/dev/video-dec0`, `/dev/video-enc0`, `/dev/mpp_service`, `/dev/rga` |

---

## 架构变更

```
Before (Master):
  cv::VideoCapture (libx264 SW decode)
       → cv::Mat → encryptFrame → cv::Mat
       → cv::VideoWriter (libx264 SW encode)

After (Feature):
  FFmpeg avcodec (SW decode, 多线程+NEON)
       → sws_scale (YUV→BGR) → cv::Mat → encryptFrame → cv::Mat
       → sws_scale (BGR→YUV) → FFmpeg avcodec (libx264 NEON SW encode)
```

加密核心 `encryptFrame()` 未做任何改动。新增 `VPUDecoder` / `VPUEncoder` 两个类，接口贴近 OpenCV 以最小化调用方改动。

---

## A/B 性能对比

### 完整管线吞吐量 (fps)

| 分辨率 | 线程数 | Master (OpenCV) | Feature (FFmpeg SW) | 提升 |
|--------|-------|----------------|---------------------|------|
| 720p | 4 | 59.8 fps | **107.5 fps** | **+80%** |
| 1080p | 1 | 31.5 fps | **57.4 fps** | **+82%** |
| 1080p | 4 | 31.0 fps | **58.2 fps** | **+88%** |
| 1080p | 8 | 30.5 fps | **56.6 fps** | **+86%** |
| 4K | 4 | 7.9 fps | **13.3 fps** | **+68%** |

### 各阶段耗时 (1080p, 4 线程)

| 阶段 | Master (OpenCV) | Feature (FFmpeg) | 改善 |
|------|----------------|-------------------|------|
| Reader (解码) | 9669 ms | 5060 ms | **1.9x** |
| Encrypt (加密) | ~2 ms | ~0.2 ms | — |
| Writer (编码) | 9676 ms | 5156 ms | **1.9x** |
| **总耗时** | **9677 ms** | **5156 ms** | **1.88x** |

### 纯加密性能（内存中，绕过编解码器）

| 分辨率 | 1 线程 | 8 线程 |
|--------|-------|--------|
| 720p | 662 MB/s | 3116 MB/s |
| 1080p | 834 MB/s | 3116 MB/s |
| 4K | 808 MB/s | 3171 MB/s |

---

## 瓶颈分析

### Master 分支
```
管线耗时分布 (1080p):
  Reader (libx264 SW decode):  ████████████████████████████████ ~97%
  Encrypt (XOR):               ░ ~0.2%
  Writer (libx264 SW encode):  ████████████████████████████████ ~97%
  
readQueue 峰值深度 = 1 → Reader 是绝对瓶颈
```

### Feature 分支
```
管线耗时分布 (1080p):
  Reader (FFmpeg SW decode):   ████████████████ ~49%
  Encrypt (XOR):               ░ ~0.01%
  Writer (libx264 SW encode):  █████████████████ ~51%
  
readQueue 峰值深度 = 1-2 → 瓶颈已部分从 Reader 转移到 Writer
```

### 关键发现

1. **纯加密能力 3116 MB/s**，管线仅发挥 ~220 MB/s（仅 7%利用率）
2. 瓶颈在编解码器，不在加密核心
3. 增加加密线程数对整体吞吐量无帮助（管线被 I/O 限制）

---

## VPU 硬件加速现状

### 硬件可用性

| 组件 | 设备节点 | 状态 |
|------|---------|------|
| VPU 解码器 | `/dev/video-dec0` | 存在 |
| VPU 编码器 | `/dev/video-enc0` | 存在 |
| MPP 服务 | `/dev/mpp_service` | 存在 |
| RGA (2D加速) | `/dev/rga` | 存在 |
| MPP 库 | `librockchip_mpp.so.1` | 已安装 |
| RGA 库 | `librga.so` | 已安装 |

### FFmpeg rkmpp 集成问题

| 组件 | FFmpeg 名称 | 状态 | 原因 |
|------|-----------|------|------|
| 解码器 | `h264_rkmpp` | **不可用** | `avcodec_open2` 成功但 `avcodec_receive_frame` 永远不产出帧 (FFmpeg 4.4 bug) |
| 编码器 | `h264_v4l2m2m` | **不可用** | 找不到 V4L2 M2M 设备（此板通过 MPP 而非 V4L2 暴露 VPU） |
| 编码器 | `h264_rkmpp` (encoder) | **不存在** | FFmpeg 4.4 未编译 rkmpp 编码器 wrapper |

### 可用的硬件加速路径

1. **升级 FFmpeg 到 5.x+** — 新版修复了 rkmpp wrapper 的 bug
2. **直接调用 MPP API** (`librockchip_mpp.so`) — 绕过 FFmpeg，直接使用 Rockchip 原生接口做硬解码/硬编码
3. **配置内核 V4L2 M2M 驱动** — 使 VPU 以标准 V4L2 接口暴露，从而兼容 `h264_v4l2m2m`

---

## 修改文件清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| `include/vpu_io.h` | 新增 | VPUDecoder / VPUEncoder 类声明 |
| `src/vpu_io.cpp` | 新增 | FFmpeg 视频 I/O 实现（SW 解码/编码 + HW 自动探测/回退） |
| `src/main.cpp` | 修改 | `cv::VideoCapture` → `VPUDecoder`, `cv::VideoWriter` → `VPUEncoder` |
| `CMakeLists.txt` | 修改 | 添加 `PkgConfig` 查找 libavcodec/format/util/swscale 并链接 |

---

## 正确性验证

| 测试项 | 结果 |
|--------|------|
| 内存级 Roundtrip (encryptFrame) | 最大像素差异 = 0（逐像素完美还原） |
| 构建 (Release -O3) | 通过 |
| test_frame 单帧质量评估 | 通过（熵 7.987, 相关性 ≈0, UACI 33.7%） |

---

## 下一步建议

| 优先级 | 方向 | 预期提升 |
|--------|------|---------|
| P0 | 直接集成 MPP API (`librockchip_mpp.so`) 做硬解码 | 管线吞吐提升 **5-10x** |
| P1 | 集成 RGA (`librga.so`) 做硬件色彩空间转换 (NV12↔BGR) | 消除 sws_scale CPU 开销 |
| P2 | 升级 FFmpeg 到 5.x 或 6.x 以修复 rkmpp wrapper | 简化集成路径 |
| P3 | 无损编码器输出 (FFV1) 保证 roundtrip 正确性 | 质量保证 |
