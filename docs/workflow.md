# 跨平台开发工作流

## 架构分工

```
┌─ x86 开发服务器 ─────────────────────┐
│  代码编写 / 编译验证 / 单元测试        │
│  OpenCV 4.x (软件编解码)              │
│  GTest 系统包 (libgtest-dev)          │
│                                       │
│  ✓ 编译 app + test_frame              │
│  ✓ 运行 13 个单元测试 (3 suites)      │
│  ✓ 快速迭代，git commit & push        │
└──────────────┬────────────────────────┘
               │  git pull
┌──────────────▼─ RK3588 开发板 ────────┐
│  最终构建 / 视频处理 / 性能测试        │
│  OpenCV + FFmpeg + rkmpp (VPU 硬件)   │
│  GTest 通过 FetchContent 自动下载     │
│                                       │
│  ✓ VPU 硬件 H.264 编解码              │
│  ✓ 8 核 ARM 全速加密                  │
│  ✓ Benchmark 报告（实际性能数据）      │
└───────────────────────────────────────┘
```

## 日常开发流程

### 在 x86 服务器上

```bash
# 1. 拉取最新代码
cd ~/workspace/VideoChaosCipher
git pull

# 2. 构建（含单元测试）
mkdir -p build && cd build
cmake .. -DBUILD_TESTS=ON
make -j$(nproc)

# 3. 运行测试
./tests/test_encryptor    # 加密正确性
./tests/test_safequeue    # 线程安全队列
./tests/test_analyzer     # 质量指标

# 4. 运行质量演示
./test_frame              # 单帧加密切换 + 质量报告

# 5. 提交
git add -A && git commit -m "feat: xxx" && git push
```

### 在 RK3588 开发板上

```bash
# 1. 拉取代码
cd ~/Work/VideoChaosCipher
git pull

# 2. 构建（VPU 硬件加速）
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# 3. 运行（实际视频处理）
./app input.mp4 output.mp4 2.0 4.0 0.77 -t 4

# 4. 性能对比
./app input.mp4 out1.mp4 -t 1
./app input.mp4 out2.mp4 -t 2
./app input.mp4 out4.mp4 -t 4
./app input.mp4 out8.mp4 -t 8
# 对比 Benchmark 报告中的 Encrypt time 和 Throughput
```

## 测试体系

| 测试套件 | Case 数 | 验证内容 | 运行位置 |
|---------|--------|---------|---------|
| test_encryptor | 3 | Roundtrip 逐像素还原、种子敏感性、加密效果 | x86 + RK3588 |
| test_safequeue | 5 | FIFO 顺序、finished 信号、生产者-消费者、多消费者并发 | x86 + RK3588 |
| test_analyzer | 5 | 熵计算、加密后高熵、原图高相关、加密后低相关、NPCR | x86 + RK3588 |

> 注：13 个单元测试不依赖真实视频文件，所用的测试图像均在内存中生成。因此可以在 x86 服务器上完整运行，无需任何外部数据。

## 环境差异说明

| 项目 | x86 服务器 | RK3588 开发板 |
|------|-----------|--------------|
| CPU | Intel/AMD x86_64 | ARM Cortex-A76×4 + A55×4 |
| 视频解码 | FFmpeg 软件解码 | VPU 硬件解码 (rkmpp) |
| 视频编码 | FFmpeg 软件编码 (avc1) | VPU 硬件编码 (rkmpp) |
| GTest 来源 | `apt install libgtest-dev` | CMake FetchContent 自动下载 |
| OpenCV 来源 | `apt install libopencv-dev` | 源码编译 (FFmpeg+rkmpp) |
| 主要用途 | 开发/调试/CI | 实际视频处理/性能测试 |

## 注意事项

1. **加密结果是跨平台一致的**：Logistic Map 使用 `float` 类型，IEEE 754 标准下 x86 和 ARM 的浮点运算结果相同。同一 seed 在不同平台上产生相同的混沌序列。

2. **test_frame 需要从项目根目录运行**：因为图片路径使用相对路径 `data/frame10.jpg`。
   ```bash
   cd ~/workspace/VideoChaosCipher && ./build/test_frame
   ```

3. **此 x86 服务器没有实际视频文件**：`app` 可以编译通过，但需要 `.mp4` 文件才能运行。实际的视频处理测试在 RK3588 上进行。
