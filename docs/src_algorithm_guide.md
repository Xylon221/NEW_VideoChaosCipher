# VideoChaosCipher 源码算法与逻辑详解

> 基于 `feature/vpu-ffmpeg-accel` 分支，逐文件解析 `src/` 目录下每个模块的算法思路、数据结构与执行流程。

---

## 目录

1. [总体架构](#1-总体架构)
2. [main.cpp — 程序入口](#2-maincpp--程序入口)
3. [pipeline.cpp — 管线核心](#3-pipelinecpp--管线核心)
4. [encryptor.cpp — 混沌加密算法](#4-encryptorcpp--混沌加密算法)
5. [reader.cpp — 视频帧读取线程](#5-readercpp--视频帧读取线程)
6. [writer.cpp — 视频帧写入线程](#6-writercpp--视频帧写入线程)
7. [vpu_io.cpp — FFmpeg 硬件编解码](#7-vpu_iocpp--FFmpeg-硬件编解码)
8. [analyzer.cpp — 加密质量评估](#8-analyzercpp--加密质量评估)
9. [stats.h + SafeQueue.h — 数据基础设施](#9-statsh--safequeueh--数据基础设施)
10. [完整数据流走读](#10-完整数据流走读)

---

## 1. 总体架构

```
输入视频.mp4
    │
    ▼
┌───────────┐   readQueue(SafeQueue)   ┌──────────────┐   writeQueue(SafeQueue)   ┌───────────┐
│  Reader   │ ───────────────────────→ │  Encryptor×N │ ───────────────────────→ │  Writer   │
│ (1 线程)  │                          │  (N 线程)     │                          │ (1 线程)  │
│ 解码帧    │                          │  XOR 混沌加密 │                          │ 排序+编码 │
└───────────┘                          └──────────────┘                          └───────────┘
                                                                                       │
                                                                                       ▼
                                                                                输出视频.mp4
```

**调用关系**:

```
main.cpp
  └→ processVideo()  [pipeline.cpp]
       ├→ readerThread()      [reader.cpp]
       ├→ encryptThread() ×N  [pipeline.cpp]
       │    └→ encryptFrame() [encryptor.cpp]
       └→ writerThread()      [writer.cpp]
```

**核心数据结构** (定义在 [stats.h](../include/stats.h)):

```cpp
struct FrameData {
    cv::Mat frame;       // BGR 像素数据 (CV_8UC3)
    int frame_index;     // 帧序号, 从 0 开始
};

struct BenchStats {
    atomic<int> readQueueDepth;       // 读队列瞬时深度
    atomic<int> readQueuePeak;        // 读队列历史峰值
    atomic<int> writeQueueDepth;      // 写队列瞬时深度
    atomic<int> writeQueuePeak;       // 写队列历史峰值
    atomic<int> totalFramesEncrypted; // 累计加密帧数
    double readerTimeMs;              // Reader 线程耗时
    double encryptTimeMs;             // 加密线程耗时
    double writerTimeMs;              // Writer 线程耗时
};
```

---

## 2. main.cpp — 程序入口

**文件**: [src/main.cpp](../src/main.cpp) (65 行)

### 2.1 职责

纯粹的参数解析 + 调用分发，不包含任何业务逻辑。

### 2.2 命令行格式

```
./app <input> <output> [start_sec] [end_sec] [seed] [-t threads]
```

| 参数 | 默认值 | 含义 |
|------|--------|------|
| `input` | 必选 | 输入视频路径 |
| `output` | 必选 | 输出视频路径 |
| `start_sec` | 2.0 | 加密起始时间(秒) |
| `end_sec` | 4.0 | 加密结束时间(秒) |
| `seed` | 0.5 | 混沌种子 (密钥) |
| `-t` | 1 | 加密线程数 |

### 2.3 执行流程

```
解析参数 → 回显参数 → 计时开始 → processVideo() → 计时结束 → 打印总耗时
```

加密与解密是**同一命令不同输出路径**:

```
加密: ./app input.mp4 encrypted.mp4  2.0 10.0 0.5 -t 4
解密: ./app encrypted.mp4 restored.mp4 2.0 10.0 0.5 -t 4
```

### 2.4 关键代码

```cpp
// 参数解析: -t 选项特殊处理，其余按位置分配
for (int i = 3; i < argc; ++i) {
    if (arg == "-t" && i + 1 < argc)
        numThreads = std::atoi(argv[++i]);
    else
        positional.push_back(arg);
}

// 调用主流程
processVideo(input_video, output_video, start_sec, end_sec, seed, numThreads);
```

---

## 3. pipeline.cpp — 管线核心

**文件**: [src/pipeline.cpp](../src/pipeline.cpp) (267 行)

### 3.1 职责

整个项目的"大脑": 组装三段流水线，管理线程生命周期，收集性能统计并打印报表。

### 3.2 核心函数: `processVideo()`

**执行流程**:

```
1. VPUDecoder::open()    — 打开输入视频
2. VPUEncoder::open()    — 创建输出文件
3. 创建 readQueue, writeQueue 两个无界阻塞队列
4. 时间 → 帧号转换: start_idx = start_sec * fps
5. 启动 Reader 线程
6. 启动 N 个 Encryptor 线程
7. 启动 Writer 线程
8. reader.join()          — 等待 Reader 结束
9. encryptors.join()      — 等待所有 Encryptor 结束
10. writeQueue.setFinished() — 通知 Writer "不再有新帧"
11. writerT.join()        — 等待 Writer 结束
12. 计算耗时 / 吞吐量
13. 打印性能报告
14. dec.release() / writer.release()
```

**为什么 join 顺序是 Reader → Encryptors → Writer**:

```
Reader 先退出 → readQueue.setFinished() 唤醒所有 Encryptor
所有 Encryptor 退出后 → writeQueue.setFinished() 唤醒 Writer
Writer 退出
```

### 3.3 核心函数: `encryptThread()`

每个 Encryptor 线程的运行逻辑:

```
while (readQueue.pop(data)) {           // 阻塞等待, 拿一帧
    readQueueDepth--                     // 读队列深度 -1
    if (帧在加密范围内) {
        encryptFrame(data.frame, seed)   // 原地 XOR 加密
        localEncrypted++
    }
    writeQueueDepth++                    // 写队列深度 +1
    writeQueue.push(data)                // 推入写队列 (可能阻塞 Writer 消费)
}
totalFramesEncrypted += localEncrypted   // 退出时汇总
```

**加密范围控制**:

- 不是全视频加密，而是 `[start_idx, end_idx]` 范围内的帧才加密
- 范围外帧直接透传 (pass-through)
- 好处: 用户可精准控制加密区间，且解密只需处理同范围

### 3.4 性能统计计算

**耗时**: 各阶段用 `high_resolution_clock::now()` 打时间戳，join 后做差。

```
readerTimeMs  = t(reader.join完成)  - t(reader启动)
encryptTimeMs = t(encrypt全部join)  - t(encrypt启动)
writerTimeMs  = t(writer.join完成)  - t(writer启动)
totalTimeMs   = t(writer.join完成)  - t(总起点)
```

注意: 前三段时间是**重叠的** (pipeline 并发)，所以 `readerTimeMs + encryptTimeMs + writerTimeMs > totalTimeMs`。

**吞吐量**:

```
整体吞吐 = totalFrames / totalTimeMs * 1000            (帧/秒)
加密吞吐 = encryptedFrames * (width*height*3/1024²)     (MB/s)
         / encryptTimeMs * 1000
```

**队列深度**:

- `readQueueDepth`: Reader ++ (入队), Encryptor -- (出队)
- `writeQueueDepth`: Encryptor ++ (入队), Writer -- (出队)
- 各线程追踪峰值: 每个写入操作后比较并更新 peak
- 队列深度反映**背压**: peak 大说明上下游速度不匹配

---

## 4. encryptor.cpp — 混沌加密算法

**文件**: [src/encryptor.cpp](../src/encryptor.cpp) (72 行)

### 4.1 算法原理

这是一个基于 **Logistic Map 混沌映射** 的流密码 (stream cipher)。

**第一步: 生成混沌序列**

```
数学公式: x_{n+1} = r × x_n × (1 − x_n)

其中: x ∈ (0, 1), r = 3.999
当 r ∈ (3.57, 4] 时系统处于完全混沌态
```

**第二步: 混沌值 → 密钥字节**

```
key_byte = ⌊chaos_value × 256⌋    →  取值范围 [0, 255]
```

**第三步: XOR 加密**

```
encrypted_pixel = plaintext_pixel ^ key_byte
```

### 4.2 加解密合一

XOR 的自逆性使得同一函数同时用于加密和解密:

```
加密: plain  ^ key = cipher
解密: cipher ^ key = cipher ^ key = plain   ← 完全恢复
```

所以调用方式为:

```cpp
encryptFrame(frame, 0.5);   // 加密 (seed=0.5)
encryptFrame(frame, 0.5);   // 解密 (同一个 seed)
```

### 4.3 逐像素加密举例

假设 seed = 0.5, 第一个像素 RGB = (100, 150, 200):

```
迭代1: x₁ = 3.999 × 0.5 × (1 − 0.5) = 0.99975
       key₁ = ⌊0.99975 × 256⌋ = 255

       R' = 100 ^ 255 = 155
       G' = 150 ^ 255 = 105
       B' = 200 ^ 255 = 55
       → 像素变成 (155, 105, 55)

迭代2: x₂ = 3.999 × 0.99975 × (1 − 0.99975) = 0.000999
       key₂ = ⌊0.000999 × 256⌋ = 0
       → 下一像素 XOR 0, 不变
```

**解密验证**:

```
155 ^ 255 = 100 ✓
105 ^ 255 = 150 ✓
 55 ^ 255 = 200 ✓
```

### 4.4 算法特点

| 特性 | 说明 |
|------|------|
| 确定性 | 相同 seed → 相同序列 → 可解密 |
| 初值敏感性 | seed 差 0.0000001 → 完全不同的序列 (蝴蝶效应) |
| 空间复杂度 | O(1), 不需要预先生成整个序列 |
| 时间复杂度 | O(rows × cols), 每像素 1 次乘法 + 1 次减法 |
| 密钥空间 | float seed 约 2^23 有效值, 远超 rand() |

### 4.5 代码结构

```cpp
float logisticMap(float x, float r) {
    return r * x * (1 - x);        // 混沌迭代公式
}

void encryptFrame(cv::Mat &frame, float seed) {
    float chaosValue = seed;
    for (每个像素) {
        chaosValue = logisticMap(chaosValue, 3.999f);  // 迭代
        uchar key = chaosValue * 256;                   // 映射到 [0,255]
        pixel[0] ^= key;  // B
        pixel[1] ^= key;  // G
        pixel[2] ^= key;  // R
    }
}
```

---

## 5. reader.cpp — 视频帧读取线程

**文件**: [src/reader.cpp](../src/reader.cpp) (57 行)

### 5.1 职责

Pipeline 的第一阶段: Producer。

### 5.2 执行流程

```
循环 {
    dec.read(frame)            ① VPU解码一帧 → BGR24 cv::Mat
    FrameData{frame, index}    ② 包上帧号
    readQueueDepth++           ③ 更新队列深度 + 峰值
    readQueue.push(data)       ④ 推入读队列 (唤醒一个Encryptor)
}
readQueue.setFinished()        ⑤ 全部帧读完 → 通知所有消费者退出
```

### 5.3 关键设计点

**为什么是单线程**: 解码器内部不是线程安全的，多线程解码需要多个 decoder 实例。

**队列深度跟踪**:

```cpp
int d = ++stats.readQueueDepth;         // atomic ++
if (d > stats.readQueuePeak.load())     // 记录峰值
    stats.readQueuePeak.store(d);
```

**退出信号**: `readQueue.setFinished()` 会设置 `finished_ = true` 并 `notify_all()`，所有阻塞在 `readQueue.pop()` 上的 Encryptor 会醒来发现队列空 + finished，从而退出循环。

---

## 6. writer.cpp — 视频帧写入线程

**文件**: [src/writer.cpp](../src/writer.cpp) (74 行)

### 6.1 职责

Pipeline 的第三阶段: Consumer。

### 6.2 核心问题: 乱序重排

**为什么帧会乱序到达**: 多个 Encryptor 并行处理，完成顺序不确定:

```
Encryptor-0 处理帧 #0 (大帧,慢) → 后到
Encryptor-1 处理帧 #1 (小帧,快) → 先到
如果直接写入 → 输出帧序错误
```

**解决方案**: `std::map<int, FrameData>` 做重排缓冲区。

### 6.3 逐帧推演

假设 Writer 收到顺序为 `{#2, #0, #4, #1, #3}`:

```
收到 #2: buffer = {2}
         begin()=2 ≠ nextToWrite(0) → 不写

收到 #0: buffer = {0, 2}
         begin()=0 = nextToWrite(0) → 写出0, nextToWrite=1
         begin()=2 ≠ 1 → 停止

收到 #4: buffer = {2, 4}
         begin()=2 ≠ nextToWrite(1) → 不写

收到 #1: buffer = {1, 2, 4}
         begin()=1 = nextToWrite(1) → 写出1, nextToWrite=2
         begin()=2 = 2            → 写出2, nextToWrite=3
         begin()=4 ≠ 3 → 停止

收到 #3: buffer = {3, 4}
         begin()=3 = nextToWrite(3) → 写出3, nextToWrite=4
         begin()=4 = 4            → 写出4, nextToWrite=5

buffer 空, 全部写入完成
```

### 6.4 为什么用 map 而非 unordered_map

`std::map` 的 `begin()` 始终返回最小 key (最小帧号)，这是 O(1) 操作。`unordered_map` 无序，每次找最小帧号需 O(N) 全量扫描。

### 6.5 核心循环代码

```cpp
while (writeQueue.pop(data)) {
    reorderBuffer[data.frame_index] = std::move(data);  // ① 插入 (自动排序)

    // ② 队首是下一个期望帧? 是则连续写出
    while (!reorderBuffer.empty() &&
           reorderBuffer.begin()->first == nextToWrite) {
        writer.write(reorderBuffer.begin()->second.frame);  // ③ 编码写入
        reorderBuffer.erase(reorderBuffer.begin());         // ④ 弹出
        nextToWrite++;                                      // ⑤ 期望帧号+1
    }
}

// 冲刷残留帧
for (auto &pair : reorderBuffer)
    writer.write(pair.second.frame);
```

---

## 7. vpu_io.cpp — FFmpeg 硬件编解码

**文件**: [src/vpu_io.cpp](../src/vpu_io.cpp) (536 行)

### 7.1 FFmpeg 抽象层速览

```
AVFormatContext  — 容器 (mp4/avi 文件级信息)
  └─ AVStream    — 流 (一个视频 track)
       └─ AVCodecContext — 编解码器上下文 (参数+状态)
            └─ AVCodec   — 编解码器算法 (h264, etc.)

AVPacket — 压缩数据包 (编码后的比特流)
AVFrame  — 未压缩帧 (YUV/BGR 像素数据)
SwsContext — 像素格式转换器 (YUV ↔ BGR)
```

### 7.2 数据流格式转换链

```
解码: YUV420P/NV12/DRM_PRIME → [sws_scale] → BGR24 → cv::Mat
编码: cv::Mat BGR24 → [sws_scale] → YUV420P → H.264
```

选 BGR24 作为中间格式是因为 OpenCV 原生 BGR 排列，避免额外转换。

### 7.3 硬件加速策略

| 阶段 | 方案 | 原因 |
|------|------|------|
| 解码 | SW (ARM NEON 多线程) | `h264_rkmpp` 在 FFmpeg 4.4 有 bug: open 成功但不产出帧 |
| 编码 | 优先 rkmpp/v4l2m2m → 回退 libx264 | HW 编码在 RK3588 上稳定可用 |

### 7.4 VPUDecoder::open() 流程

```
1. avformat_open_input()        打开文件
2. avformat_find_stream_info()  探测流信息 (读若干帧确定参数)
3. 遍历 streams[] 找到视频流索引 (codec_type == AVMEDIA_TYPE_VIDEO)
4. 获取 width, height, fps, frameCount
5. avcodec_find_decoder()       查找解码器
6. avcodec_open2()              打开解码器 (thread_count=0 → 自动多线程)
7. 分配三层帧缓冲区:
   - frame    解码输出 (可能是 GPU 帧)
   - swFrame  HW→SW 传输帧 (仅 DRM_PRIME 格式需要)
   - bgrFrame BGR24 目标帧 (给 OpenCV 用)
8. sws_getContext()             创建 YUV→BGR 转换器
```

### 7.5 VPUDecoder::read() — FFmpeg send/receive 模型

```
while (true) {
    av_read_frame()           → 读压缩包 (AVPacket)
    if (不是视频包) continue   → 跳过音频/字幕

    avcodec_send_packet()     → 送入解码器
    avcodec_receive_frame()   → 取解码后帧 (AVFrame)
    if (EAGAIN) continue      → 需要更多包 (B帧要等后续帧)
    if (ret < 0) return false → 解码错误

    if (DRM_PRIME格式)        → av_hwframe_transfer_data() GPU→CPU

    sws_scale()               → YUV/NV12 → BGR24

    cv::Mat(...).clone()      → 包装为 OpenCV 矩阵 (必须clone!)
}
```

**关键**: send 和 receive **不是 1:1 的**:

- 可能需要 send 多次才 receive 一次 (B 帧需参考未来帧)
- 也可能 send 一次 receive 多次 (解码器缓冲)
- `EAGAIN` 表示当前无可用帧，需要继续 send

**为什么 clone() 必须**: `bgrFrame` 是复用缓冲区，不 clone 下一帧会覆盖当前帧数据。

### 7.6 VPUEncoder::open() 流程

```
1. avformat_alloc_output_context2()  创建输出容器 (mp4)
2. 查找编码器:
   优先: h264_rkmpp (Rockchip MPP)
   其次: h264_v4l2m2m (V4L2 内存到内存)
   兜底: libx264 (软件)
3. 配置编码参数:
   - 码率: width * height * 4 bps (1080p → ~8.3Mbps)
   - GOP: 30 (每30帧一个I帧)
   - B帧: 0 (降低延迟)
   - thread_count: 0 (自动)
4. avcodec_open2() → 失败则回退到 libx264
5. avformat_write_header()  写文件头
6. 创建 BGR→YUV 转换器 (sws_getContext)
```

### 7.7 VPUEncoder::write() 流程

```
cv::Mat BGR24 → sws_scale() → YUV420P AVFrame
  → avcodec_send_frame()         送入编码器
  → while avcodec_receive_packet():
       av_interleaved_write_frame()   写入文件
```

### 7.8 VPUEncoder::release() 流程

```
avcodec_send_frame(nullptr)     → 冲刷编码器 (输出缓冲的延迟帧)
av_write_trailer()              → 写文件尾 (moov atom等)
释放 swsCtx → yuvFrame → packet → codecCtx → fmtCtx
```

### 7.9 Pimpl 模式

**为什么用**: 头文件 `vpu_io.h` 不暴露 FFmpeg 类型，调用方不需 `#include` FFmpeg 头。编译隔离，接口稳定。

```cpp
// vpu_io.h — 公开接口
struct VPUDecoderImpl;     // 仅前置声明
class VPUDecoder {
    VPUDecoderImpl *impl;  // 私有指针
};

// vpu_io.cpp — 实现细节
struct VPUDecoderImpl {    // 完整定义在这里
    AVFormatContext *fmtCtx;
    AVCodecContext *codecCtx;
    // ...
};
```

---

## 8. analyzer.cpp — 加密质量评估

**文件**: [src/analyzer.cpp](../src/analyzer.cpp) (240 行)

### 8.1 5 项指标总览

| 指标 | 测试目标 | 理想值 | 公式 |
|------|----------|--------|------|
| Entropy | 统计均匀性 | 8.0 bit | H = −Σ p(k)·log₂(p(k)) |
| Histogram Var | 直方图平坦度 | < 500 | Σ(observed − expected)² / (3×256) |
| Correlation | 空间去相关 | ≈ 0 | Pearson r |
| NPCR | 差分敏感性 | > 99.6% | diff_pixels / total × 100% |
| UACI | 变化强度 | ≈ 33.3% | Σ|C1−C2| / (W×H×3×255) × 100% |

### 8.2 信息熵 (calcEntropy)

**直觉**: 衡量像素值的"混乱程度"。完全均匀分布 → 8.0 bit; 全图同色 → 0 bit。

```
算法:
  对每个通道:
    统计 256 个灰度级的出现次数 hist[256]
    对每个 k:
      p = hist[k] / totalPixels
      H -= p * log2(p)
  返回三通道平均值
```

### 8.3 直方图方差 (calcHistogramVariance)

**直觉**: 衡量直方图偏离均匀分布的程度。方差越小越平坦。

```
expected = totalPixels / 256          理想均匀分布值
variance = Σ(hist[k] - expected)² / (3 × 256)
```

### 8.4 相邻像素相关系数 (calcCorrelation)

**直觉**: 原图相邻像素颜色接近 (r > 0.9)；加密后完全随机 (r ≈ 0)。

```
算法:
  随机采样 N=3000 对相邻像素 (根据方向设 dx,dy 偏移)
  取灰度值 = (R+G+B)/3
  计算 Pearson 相关系数: r = Cov(X,Y) / sqrt(Var(X)·Var(Y))
```

三个方向独立评估: horizontal / vertical / diagonal。

### 8.5 NPCR (calcNPCR)

**直觉**: 像素改变率。改 1 个明文像素后密文有 99.6%+ 的像素不同。

```
NPCR = (different_pixel_count / total_pixels) × 100%
```

### 8.6 UACI (calcUACI)

**直觉**: 平均变化强度。不只是"变了"，而且"变得够大"。

```
UACI = Σ|C1(i,j,k) − C2(i,j,k)| / (W×H×3×255) × 100%
```

---

## 9. stats.h + SafeQueue.h — 数据基础设施

### 9.1 stats.h — 数据契约

**文件**: [include/stats.h](../include/stats.h)

两个结构体定义了整个 pipeline 的"流通货币":

- `FrameData`: 帧数据 + 帧号, 在三个阶段之间流动
- `BenchStats`: 所有性能统计汇总于此, `atomic<int>` 保证无锁并发读写

### 9.2 SafeQueue.h — MPMC 阻塞队列

**文件**: [include/SafeQueue.h](../include/SafeQueue.h)

线程安全的多生产者-多消费者 (MPMC) 阻塞队列模板。

**三个核心方法**:

| 方法 | 行为 | 调用者 |
|------|------|--------|
| `push(v)` | 加锁 → move 入队 → `notify_one()` | Producer |
| `pop(v)` | 加锁 → 等队列非空或 finished → move 出队 → 返回 true/false | Consumer |
| `setFinished()` | 设 finished=true → `notify_all()` | 唯一的 Producer |

**退出机制**:

```
Producer 读完所有数据 → setFinished() → notify_all()
所有 Consumer 被唤醒 → 发现 queue.empty() && finished_ → pop() 返回 false → 线程退出
```

**为什么 pop 用 notify_one 但 setFinished 用 notify_all**:

- `push` 只需唤醒 1 个消费者 (避免惊群)
- `setFinished` 必须唤醒**所有**消费者 (每个都要看到 finished 并退出)

**本项目中的两个实例**:

```
readQueue:  Reader(1 producer) → Encryptors(N consumers)
writeQueue: Encryptors(N producers) → Writer(1 consumer)
```

---

## 10. 完整数据流走读

以 `./app input.mp4 output.mp4 2.0 4.0 0.5 -t 4` 为例走一遍:

```
main.cpp:
  解析参数:
    input=input.mp4, output=output.mp4
    start_sec=2.0, end_sec=4.0, seed=0.5, numThreads=4
  → processVideo(...)

pipeline.cpp:processVideo():
  1. VPUDecoder::open("input.mp4")
       avformat_open_input → 探测流 → 找到H.264视频流
       → 创建软件解码器(ARM NEON 多线程)
       → 分配帧缓冲 → 创建YUV→BGR转换器
       读取: fps=30, 1920×1080, totalFrames=300

  2. VPUEncoder::open("output.mp4", 1920, 1080, 30)
       → 优先h264_rkmpp → 失败回退libx264
       → 配置8.3Mbps码率 → 创建BGR→YUV转换器

  3. 帧号范围: start_idx=60, end_idx=120 (2.0~4.0秒 × 30fps)

  4. 启动线程 (全部并发运行):

     Thread[Reader]:
       index=0: av_read_frame → 跳过音频包 → send → receive → YUV→BGR → cv::Mat
       → FrameData{frame0, 0} → readQueue.push()  [readQueueDepth=1]
       index=1: ... FrameData{frame1, 1} → push    [readQueueDepth=2]
       ... 持续解码全300帧 ...
       index=299: FrameData{frame299, 299} → push
       → readQueue.setFinished()

     Thread[Encryptor-0]:
       readQueue.pop() → 拿到 frame0
       frame_index(0) < start_idx(60) → 透传, 不加密
       writeQueue.push(frame0)  [writeQueueDepth=1]

     Thread[Encryptor-1]:
       readQueue.pop() → 拿到 frame1
       frame_index(1) < 60 → 透传
       writeQueue.push(frame1)  [writeQueueDepth=2]

     ... Encryptor 们并行取帧 ...

     当某个 Encryptor 拿到 frame70 (在60~120范围):
       encryptFrame(frame70, 0.5):
         chaosValue=0.5
         遍历1920×1080=2,073,600个像素:
           迭代logisticMap → 生成key → XOR B/G/R
       writeQueue.push(frame70)

     ... Encryptor 继续处理直到 readQueue 空 ...

     Thread[Writer]:
       writeQueue.pop() → 拿到 frame2 (Encryptor-1先完成)
       reorderBuffer = {2: frame2}
       begin()=2 ≠ nextToWrite(0) → 等待

       pop() → 拿到 frame0 (Encryptor-0后完成)
       reorderBuffer = {0: frame0, 2: frame2}
       begin()=0 = nextToWrite(0) → VPUEncoder::write(frame0)
         BGR→YUV→H.264编码 → av_interleaved_write_frame → 写入output.mp4
       nextToWrite=1, begin()=2 ≠ 1 → 等待

       ... 持续取帧、排序、写入 ...

       最后 pop() 返回 false (writeQueue finished 且 空)
       冲刷 reorderBuffer 残留帧
       → Writer 线程结束

  5. reader.join()      ← Reader 先退出
  6. encryptors.join()  ← 4个Encryptor全部退出
  7. writeQueue.setFinished()
  8. writerT.join()     ← Writer 退出

  9. 计算时间:
       readerTimeMs  = 2345.12 ms    (解码300帧)
       encryptTimeMs = 2891.45 ms   (4线程加密60帧)
       writerTimeMs  = 2103.67 ms    (编码300帧)
       totalTimeMs   = 3150.83 ms    (端到端, 重叠)

  10. 打印性能报告:
       Throughput (overall): 95.24 frames/s
       Throughput (encrypt): 456.78 MB/s
       readQueue  peak: 45 frames
       writeQueue peak: 8 frames

  11. dec.release() / writer.release()
```

---

## 附录: 阅读建议

### 按难度递进

| 层级 | 文件 | 行数 | 内容 |
|------|------|------|------|
| ★☆☆ | encryptor.cpp | 72 | 核心算法, 独立, 逻辑简单 |
| ★☆☆ | reader.cpp | 57 | 线程循环, 直观 |
| ★☆☆ | main.cpp | 65 | 参数解析 |
| ★★☆ | writer.cpp | 74 | 乱序重排逻辑 |
| ★★☆ | pipeline.cpp | 267 | 管线编排, 线程管理 |
| ★★★ | vpu_io.cpp | 536 | FFmpeg 底层, Pimpl, 硬件加速 |
| ★★☆ | analyzer.cpp | 240 | 5项数学指标 |

### 建议阅读顺序

```
stats.h → SafeQueue.h → encryptor.cpp → reader.cpp
→ writer.cpp → pipeline.cpp → main.cpp → vpu_io.cpp → analyzer.cpp
```
