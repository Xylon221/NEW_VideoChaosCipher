# VideoChaosCipher

基于 Logistic Map 混沌映射的视频选择性加密工具，使用 C++ 多线程流水线实现像素级 XOR 加解密。

## 原理

利用 Logistic Map `x_{n+1} = r × x_n × (1 - x_n)` 在参数 r≈4 时的混沌特性生成伪随机序列，对视频帧每个像素的 R、G、B 通道进行 XOR 混淆。XOR 运算的自逆性质使得**同一函数即可加密也可解密**，只需传入相同的种子值。

## 依赖

- C++11 或更高版本
- OpenCV 4.x
- CMake 3.10+

## 构建

```bash
mkdir build && cd build
cmake ..
make
```

## 用法

```bash
./app <输入视频> <输出视频> [开始秒数] [结束秒数] [种子值]
```

| 参数 | 说明 | 默认值 |
|------|------|--------|
| 输入视频 | 待处理的视频文件路径 | 必填 |
| 输出视频 | 处理后的视频保存路径 | 必填 |
| 开始秒数 | 加密起始时间（秒） | 2.0 |
| 结束秒数 | 加密结束时间（秒） | 4.0 |
| 种子值 | 混沌映射初始种子 | 0.5 |

### 示例

```bash
# 加密第 2~4 秒，使用默认种子
./app input.mp4 output.mp4

# 加密第 1~3 秒，种子 0.77
./app input.mp4 output.mp4 1.0 3.0 0.77

# 解密：对加密视频再次执行相同命令（相同种子）
./app encrypted.mp4 decrypted.mp4 1.0 3.0 0.77
```

## 架构

三线程生产者-消费者流水线：

```
readerThread          encryptThread         writerThread
    │                      │                     │
    ├─ read frame ──→ readQueue ──→ pop frame ──┤
    │                                    │       │
    │                              encryptFrame  │
    │                                    │       │
    │                      push ──→ writeQueue ──├─ write frame
```

线程间通过自定义 `SafeQueue<T>` 模板类通信，基于 `std::mutex` + `std::condition_variable` 实现阻塞式同步。

## 项目结构

```
VideoChaosCipher/
├── CMakeLists.txt
├── include/
│   ├── encryptor.h        # 混沌加密函数声明
│   └── SafeQueue.h         # 线程安全队列模板
├── src/
│   ├── main.cpp            # 主程序，多线程流水线
│   ├── encryptor.cpp       # Logistic Map 加密核心
│   └── test_encryptFrame.cpp  # 单帧加密测试
└── 项目文档.md             # 详细项目文档（含面试问答）
```

## License

MIT
