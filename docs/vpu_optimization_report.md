# 视频 I/O 与 VPU 优化验证报告

> 本文用于记录当前优化状态和后续上板实测结果。旧文档中的固定性能数字只作为历史记录，不能直接作为当前版本的最终结论。当前版本应以重新运行 benchmark 后的数据为准。

## 1. 当前结论

代码层面已经完成从 OpenCV Video I/O 到 FFmpeg API 封装的升级，并实现了硬件优先、软件回退的编解码器选择策略。目标架构是：VPU 承担视频编解码，CPU 承担加密计算。

需要注意：

- WSL/x86 环境没有 RK3588 VPU，因此只能验证软件回退、线程流水线和算法正确性。
- RK3588 上是否真正使用 VPU，要看运行日志中的 `HW=yes/no`，以及 FFmpeg 是否提供 rkmpp/v4l2m2m 编解码器。
- 历史报告中约 1.8x 的提升来自早期 FFmpeg 软件路径相对 OpenCV 路径的对比，当前升级后需要重新测试。

## 2. 已完成改造

| 改造项 | 状态 | 说明 |
| --- | --- | --- |
| FFmpeg 解码封装 | 已完成 | `VPUDecoder` 使用 FFmpeg 打开文件或 v4l2 摄像头 |
| FFmpeg 编码封装 | 已完成 | `VPUEncoder` 写 H.264 输出，维护 time_base 和 PTS |
| 硬件优先选择 | 已完成 | 优先尝试 rkmpp/v4l2m2m，失败自动回退软件路径 |
| 多线程加密 | 已完成 | Reader + N Encryptor + Writer |
| 有界队列背压 | 已完成 | `-q` 可配置容量，避免内存无限增长 |
| 运行统计 | 已完成 | 统计 decoded/encrypted/written、FPS、队列峰值 |
| 单元测试 | 已完成 | 当前 15 个测试通过 |
| RK3588 实机性能数据 | 待补充 | 需要上板重新采集 |

## 3. 本地 WSL 验证结果

已验证内容：

```bash
cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTS=ON
cmake --build build-wsl -j$(nproc)
cd build-wsl && ctest --output-on-failure
```

结果：

- 构建通过。
- 15 个单元测试通过。
- 60 帧端到端烟测通过：输入 60 帧，输出 60 帧，帧率保持 30 fps。
- `-q 4` 场景下观察到队列峰值达到 4/4，说明有界队列背压生效。
- 因 WSL 无 RK3588 VPU，编解码走软件回退路径。

## 4. RK3588 上板验证计划

上板后执行：

```bash
cd /home/xylon/workspace/VideoChaosCipher
./scripts/inspect_codecs.sh
cmake -S . -B build-wsl -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build-wsl -j$(nproc)
cd build-wsl && ctest --output-on-failure
```

文件输入测试：

```bash
./build-wsl/video_encryptor input.mp4 output.mp4 0 5 -t 4 -q 16
```

摄像头测试：

```bash
./build-wsl/video_encryptor /dev/video0 camera_out.mp4 0 10 -t 4 -q 8
```

记录：

| 指标 | 记录方式 |
| --- | --- |
| Decoder 是否硬件 | `[VPUDecoder] Opened: ... (HW=yes/no)` |
| Encoder 是否硬件 | `[VPUEncoder] Opened: ... (HW=yes/no)` |
| 处理 FPS | 程序统计输出 |
| CPU 占用 | `top` / `htop` / `pidstat` |
| 队列峰值 | 程序统计输出 |
| 输出可播放性 | `ffprobe` / 播放器检查 |

## 5. Benchmark 表格模板

上板后补充以下表格：

| 平台 | 输入 | 分辨率/FPS | 编解码器日志 | 线程数 | 队列容量 | 平均 FPS | CPU 占用 | 备注 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| WSL | 测试视频 | 640x360/30 | HW=no/HW=no | 4 | 4 | 待记录 | 待记录 | 软件回退 |
| RK3588 | H.264 文件 | 待记录 | 待记录 | 1 | 16 | 待记录 | 待记录 | 单线程基线 |
| RK3588 | H.264 文件 | 待记录 | 待记录 | 4 | 16 | 待记录 | 待记录 | 多线程加密 |
| RK3588 | 摄像头 | 待记录 | 待记录 | 4 | 8 | 待记录 | 待记录 | 实时采集 |

## 6. 历史性能记录说明

旧版本曾记录过 FFmpeg 软件路径相对 OpenCV Video I/O 路径约 1.8x 的吞吐提升。该数据可以作为“历史优化方向”的参考，但不应直接写成当前最终性能结论。当前版本增加了硬件优先选择、有界队列和更完整统计，需要在同一输入、同一平台、同一线程数下重新跑 benchmark。

推荐简历写法：

> 将视频 I/O 从 OpenCV 封装替换为 FFmpeg API，补充硬件优先编解码选择和软件回退机制，并通过端到端统计对 FPS、队列峰值和帧数一致性进行验证。

不推荐写法：

> VPU 加速后固定提升 1.8 倍。

## 7. 后续优化

1. 在 RK3588 上补齐硬件路径 benchmark。
2. 如果 rkmpp wrapper 不稳定，评估直接接入 Rockchip MPP API。
3. 为摄像头增加显式分辨率、fps、pixel format 配置。
4. 增加 CSV benchmark 输出，便于画图和写简历量化结果。
5. 增加无损/码流层加密模式，区分演示加扰和严格可逆归档。
