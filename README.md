# VideoChaosCipher

**基于 RK3588 嵌入式平台的视频混沌加密系统**

利用 Logistic Map 混沌映射 + VPU 硬件视频编解码 + 多线程并行加密，对视频指定时间段进行像素级 XOR 加解密。同一函数即可加密也可解密，支持质量量化评估、性能基准报告和自动化单元测试。

```
Platform:  Orange Pi 5 (Rockchip RK3588)
VPU:       H.264/H.265 硬件编解码 (FFmpeg + rkmpp)
CPU:       4×Cortex-A76 + 4×Cortex-A55
Encryption: Logistic Map 混沌序列 + XOR
Build:     CMake 3.14+ / C++11 / OpenCV 4.x
Tests:     Google Test (FetchContent 自动下载)
```

---

## 架构概览

```
                    RK3588 SoC
  ┌─────────────────────────────────────────┐
  │  ┌──────────┐    ┌──────────────────┐   │
  │  │   VPU    │    │  8-core ARM CPU  │   │
  │  │ H.264    │    │  ┌────┐ ┌────┐   │   │
  │  │ decode/  │    │  │Enc │ │Enc │   │   │
  │  │ encode   │    │  │ #0 │ │ #1 │···│   │
  │  └────┬─────┘    │  └──┬─┘ └──┬─┘   │   │
  │       │          │     │      │      │   │
  │  ┌────▼─────────────────▼──────▼──┐   │   │
  │  │     SafeQueue<FrameData>       │   │   │
  │  │  readQueue ──→ writeQueue      │   │   │
  │  └────────────────────────────────┘   │   │
  └─────────────────────────────────────────┘

  Reader ──→ readQueue ──→ N×Encryptors ──→ writeQueue ──→ Writer
                                              (reorder buffer)
```

**异构计算分工**：VPU 处理视频编解码（CPU 零开销），A76 大核跑加密计算主力线程，A55 小核处理 I/O 调度。

---

## 快速开始

### 前置依赖

- C++11 编译器
- OpenCV 4.x（RK3588 上推荐带 FFmpeg + rkmpp 支持）
- CMake 3.14+

### 构建

```bash
# 标准构建
./scripts/build.sh

# Debug 模式 + 带单元测试
./scripts/build.sh --debug --test

# 手动构建
mkdir build && cd build
cmake .. && make -j$(nproc)
```

> **RK3588 用户**：参考 [scripts/setup_rk3588.md](scripts/setup_rk3588.md) 配置 VPU 硬件加速环境。

### 运行

```bash
./app <输入视频> <输出视频> [开始秒数] [结束秒数] [种子值] [-t 线程数]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `<输入视频>` | 待处理视频路径 | 必填 |
| `<输出视频>` | 输出视频路径 | 必填 |
| `[开始秒数]` | 加密起始秒 | 2.0 |
| `[结束秒数]` | 加密结束秒 | 4.0 |
| `[种子值]` | 混沌种子 (加密密钥) | 0.5 |
| `-t N` | 加密线程数 | 1 |

### 示例

```bash
# 默认参数：加密第 2~4 秒
./app input.mp4 output.mp4

# 自定义加密参数 + 4 线程
./app input.mp4 output.mp4 1.0 3.0 0.77 -t 4

# 解密：对加密视频再次执行相同命令
./app encrypted.mp4 decrypted.mp4 1.0 3.0 0.77 -t 4
```

### 运行测试

```bash
cd build
cmake -DBUILD_TESTS=ON .. && make
ctest --output-on-failure
```

### 单帧质量评估

```bash
./test_frame
# 输出原始图像 vs 加密图像的五项指标对比报告
```

---

## 项目结构

```
VideoChaosCipher/
├── CMakeLists.txt                  # CMake 构建 (FetchContent, install, build types)
├── include/
│   ├── encryptor.h                 # 混沌加密 API
│   ├── SafeQueue.h                 # 线程安全阻塞队列模板
│   └── analyzer.h                  # 加密质量评估 (5 项指标)
├── src/
│   ├── main.cpp                    # 主程序 (多加密线程 + Benchmark)
│   ├── encryptor.cpp               # Logistic Map 混沌加密核心
│   ├── analyzer.cpp                # 质量指标实现 + 报告输出
│   └── test_encryptFrame.cpp       # 单帧加密 + 质量报告演示
├── tests/
│   ├── CMakeLists.txt              # GTest 测试构建 (FetchContent)
│   ├── test_encryptor.cpp          # 加密 roundtrip、种子敏感性
│   ├── test_safequeue.cpp          # 队列 FIFO、多线程生产者-消费者
│   └── test_analyzer.cpp           # 指标正确性验证
├── scripts/
│   ├── build.sh                    # 一键构建脚本
│   └── setup_rk3588.md             # RK3588 VPU 环境配置指南
├── 项目文档.md                      # 详细文档 (含面试问答)
└── README.md
```

---

## 加密质量指标

程序自动输出五项密码学标准指标：

| 指标 | 原始图像 | 加密图像 | 理想值 |
|------|---------|---------|--------|
| 信息熵 (Shannon) | ~5.2 bit | ~7.997 bit | 8.0 |
| 直方图方差 | 大 | 小 | < 500 |
| 水平像素相关性 | ~0.95 | ~0.002 | 0 |
| 垂直像素相关性 | ~0.96 | ~-0.001 | 0 |
| 对角像素相关性 | ~0.94 | ~0.003 | 0 |
| NPCR | — | ~99.65% | > 99.6% |
| UACI | — | ~33.4% | ~33.3% |

---

## 性能报告

每次运行自动输出 Benchmark：

```
========================================================
              Performance Report
========================================================
  Resolution:          1920x1080
  FPS:                 30.00
  Total frames:        300
  Frames encrypted:    60  (range: 60 - 120)
  Encrypt threads:     4
--------------------------------------------------------
  Reader time:         820.00 ms
  Encrypt time:        310.00 ms  (4 threads)
  Writer time:         280.00 ms
  Total wall time:     1410.00 ms
--------------------------------------------------------
  Throughput (overall):  212.77 frames/s
  Throughput (encrypt):  1156.45 MB/s
--------------------------------------------------------
  readQueue  peak depth: 8 frames
  writeQueue peak depth: 6 frames
========================================================
```

---

## 测试

3 个 test suite，覆盖核心逻辑：

```
test_encryptor:
  Roundtrip                              PASSED  (加密→解密逐像素还原)
  DifferentSeedsProduceDifferentResults  PASSED  (seed 敏感性)
  EncryptionChangesImage                 PASSED  (加密效果验证)

test_safequeue:
  PushPop                                PASSED  (基本 FIFO)
  FifoOrder                              PASSED  (顺序保持)
  FinishedReturnsFalse                   PASSED  (结束信号)
  ProducerConsumer                       PASSED  (单生产者单消费者)
  MultipleConsumers                      PASSED  (多消费者并发)

test_analyzer:
  UniformImageEntropy                    PASSED  (纯色图熵≈0)
  EncryptedImageHighEntropy              PASSED  (加密后熵>7)
  OriginalImageHighCorrelation           PASSED  (原图高相关)
  EncryptedImageLowCorrelation           PASSED  (加密后≈0)
  NPCR                                   PASSED  (NPCR>99%)
```

---

## 技术栈

| 层 | 技术 |
|----|------|
| 语言 | C++11 |
| 视频 I/O | OpenCV 4.x + FFmpeg (RK3588 上启用 rkmpp) |
| 加密算法 | Logistic Map 混沌序列 + XOR |
| 并发 | std::thread, std::mutex, std::condition_variable, std::atomic |
| 构建 | CMake 3.14+ (FetchContent 自动下载 GTest) |
| 测试 | Google Test v1.14.0 |
| 平台 | Linux (RK3588 ARM / x86_64) |

---

## License

MIT
