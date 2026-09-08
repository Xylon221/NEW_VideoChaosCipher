# VideoChaosCipher 源码导读

> 本文用于读代码和面试复盘，按执行路径解释主要文件。当前分支重点是 FFmpeg 视频 I/O、多线程加密流水线、有界队列背压和运行统计。

## 1. 程序入口：src/main.cpp

`main.cpp` 负责解析命令行参数并启动流水线。基础参数是输入、输出、加密开始时间和结束时间；可选参数包括：

```bash
-t, --threads N   加密线程数，0 表示自动使用 CPU 并发数
-q, --queue N     队列容量，0 表示无界
-h, --help        打印帮助
```

入口层只做参数校验和调度，不直接处理视频帧。这样主流程保持清晰，具体工作交给 `pipeline.cpp`。

## 2. 流水线调度：src/pipeline.cpp

`runPipeline()` 是核心调度函数，主要步骤是：

1. 创建 `VPUDecoder` 并打开输入。
2. 获取视频宽高、fps 等信息。
3. 创建 `VPUEncoder` 并打开输出。
4. 创建 `readQueue` 和 `writeQueue`。
5. 启动 Reader、N 个 Encryptor、Writer。
6. 等待线程结束并打印统计信息。

这里的关键是生命周期管理：Reader 结束后标记 readQueue 完成；所有 Encryptor 结束后标记 writeQueue 完成；Writer 消费完剩余帧后退出。

## 3. 帧结构：include/frame_data.h

`FrameData` 把图像帧和帧序号绑定在一起：

```cpp
struct FrameData {
    cv::Mat frame;
    int index;
};
```

`index` 是后续重排的依据。多线程加密会导致完成顺序变化，但只要每帧携带编号，Writer 就能恢复原顺序。

## 4. 队列：include/SafeQueue.h

`SafeQueue<T>` 是线程安全阻塞队列，支持两种模式：

- `maxSize == 0`：无界队列。
- `maxSize > 0`：有界队列，队列满时 `push()` 阻塞。

它使用 mutex 保护内部 `std::queue`，用 condition_variable 实现等待和唤醒。`setFinished()` 表示不会再有新数据，消费者可以在队列清空后正常退出。

当前队列还会记录当前深度和峰值深度，用于观察流水线是否被某一阶段卡住。

## 5. 读帧线程：src/reader.cpp

Reader 持有解码器引用，循环调用 `decoder.read(frame)`。每读到一帧就构造 `FrameData{frame, index}` 并推入 readQueue，同时递增 decoded 统计。

如果输入是普通文件，FFmpeg 会根据封装格式和编码格式解码。如果输入路径形如 `/dev/video0`，当前实现会按 Linux v4l2 摄像头输入处理。

## 6. 加密线程：src/encryptor.cpp

Encryptor 从 readQueue 取帧，计算当前帧时间：

```text
timestamp = frameIndex / fps
```

如果 timestamp 在 `[startTime, endTime]` 内，就调用 `encryptFrame()`；否则原样传递。处理完成后推入 writeQueue。

多个 Encryptor 可以并行运行，线程数由 `-t` 指定。加密是 CPU 计算任务，适合放到多核 ARM CPU 上并行。

## 7. 写帧线程：src/writer.cpp

Writer 从 writeQueue 取帧，但不会直接写出。因为多个 Encryptor 完成顺序不可控，Writer 需要用 `std::map<int, FrameData>` 暂存帧，并维护 `nextToWrite`。

只要 map 中存在 `nextToWrite`，就写出该帧并递增 `nextToWrite`。这个设计保证输出视频顺序与输入一致。

## 8. 加密算法：src/crypto.cpp

`encryptFrame()` 对 OpenCV BGR 图像逐字节处理。核心逻辑：

1. 用 seed 初始化 Logistic Map。
2. 每次迭代生成一个 0-255 的 key byte。
3. 对像素通道执行 XOR。

XOR 的好处是实现简单、速度快、算法层面可逆。限制是安全强度不等同于 AES/ChaCha20，且经过有损视频编码后不能保证逐像素恢复。

## 9. 视频 I/O：src/vpu_io.cpp

`vpu_io.cpp` 直接使用 FFmpeg API，包括 demux、decode、scale、encode、mux 等步骤。对外接口仍叫 `VPUDecoder`/`VPUEncoder`，但准确理解应是“硬件优先的视频 I/O 封装”。

解码器选择逻辑：

- H.264/H.265 输入优先尝试 RK/V4L2 硬件解码器。
- 硬件不可用时回退默认软件解码器。
- 解码后的像素统一转换成 BGR，便于 OpenCV 和加密函数处理。

编码器选择逻辑：

- 优先尝试 `h264_rkmpp`、`h264_v4l2m2m` 等硬件编码器。
- 不可用时回退 `libx264` 或默认 H.264 编码器。
- 写帧时维护 PTS，释放时 flush encoder，保证输出文件完整。

运行时应该关注日志：

```text
[VPUDecoder] Opened: xxx (HW=yes/no)
[VPUEncoder] Opened: xxx (HW=yes/no)
```

这比文档里的任何静态说法都更可信。

## 10. 质量评估：src/analyzer.cpp

项目提供多种加密效果指标：

| 指标 | 含义 |
| --- | --- |
| NPCR | 原图和密文图像像素变化率，越高说明扰动范围越大 |
| UACI | 平均像素变化强度 |
| Correlation | 相邻像素相关性，越低表示图像结构越难被看出 |
| Entropy | 信息熵，越接近随机分布越好 |
| MSE/PSNR | 衡量图像差异，常用于图像质量评价 |

这些指标用于说明“画面是否被充分扰乱”，不是严格密码学安全证明。

## 11. 运行统计：include/stats.h

统计项包括：

- decoded frames：解码读入帧数。
- encrypted frames：实际进入加密时间段并执行加密的帧数。
- written frames：写出帧数。
- queue depth/peak：队列当前深度和峰值深度。
- elapsed time / FPS：端到端吞吐。

这些统计能帮助定位瓶颈：如果 readQueue 经常满，说明加密或写出跟不上；如果 writeQueue 经常满，说明编码器或磁盘写出可能是瓶颈。

## 12. 测试：tests/

当前测试重点：

- 加密函数能改变图像，并在算法层面支持 XOR 再次恢复。
- 质量指标计算结果合理。
- SafeQueue 支持 push/pop、完成通知、多线程消费、有界阻塞等行为。

当前 WSL 验证结果：15 个测试通过。

## 13. 建议阅读顺序

1. `src/main.cpp`：先看命令行入口。
2. `src/pipeline.cpp`：理解总体线程组织。
3. `include/SafeQueue.h`：理解线程同步和背压。
4. `src/reader.cpp`、`src/encryptor.cpp`、`src/writer.cpp`：串起完整数据流。
5. `src/crypto.cpp`：看加密算法。
6. `src/vpu_io.cpp`：最后看 FFmpeg 细节，因为这里代码最多、API 状态机也最复杂。
