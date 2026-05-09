#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "/home/orangepi/Work/VideoChaosCipher/include/SafeQueue.h"

// 基本的 push/pop 测试
TEST(SafeQueueTest, PushPop) {
    SafeQueue<int> q;
    q.push(42);
    int val = 0;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 42);
}

// FIFO 顺序测试
TEST(SafeQueueTest, FifoOrder) {
    SafeQueue<int> q;
    for (int i = 0; i < 100; ++i) {
        q.push(i);
    }
    for (int i = 0; i < 100; ++i) {
        int val = 0;
        EXPECT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}

// setFinished 后 pop 应返回 false
TEST(SafeQueueTest, FinishedReturnsFalse) {
    SafeQueue<int> q;
    q.setFinished();
    int val = 0;
    EXPECT_FALSE(q.pop(val));
}

// 生产者-消费者多线程测试
TEST(SafeQueueTest, ProducerConsumer) {
    SafeQueue<int> q;
    const int N = 1000;

    // 生产者线程
    std::thread producer([&q, N]() {
        for (int i = 0; i < N; ++i) {
            q.push(i);
        }
        q.setFinished();
    });

    // 消费者线程
    int sum = 0;
    int count = 0;
    std::thread consumer([&q, &sum, &count]() {
        int val = 0;
        while (q.pop(val)) {
            sum += val;
            count++;
        }
    });

    producer.join();
    consumer.join();

    EXPECT_EQ(count, N);
    EXPECT_EQ(sum, N * (N - 1) / 2);  // 0 + 1 + ... + (N-1)
}

// 多消费者并发
TEST(SafeQueueTest, MultipleConsumers) {
    SafeQueue<int> q;
    const int N = 1000;

    // 生产者
    std::thread producer([&q, N]() {
        for (int i = 0; i < N; ++i) {
            q.push(i);
        }
        q.setFinished();
    });

    // 2 个消费者，各自累加
    std::atomic<int> sum1{0}, sum2{0};
    std::atomic<int> cnt1{0}, cnt2{0};

    std::thread c1([&q, &sum1, &cnt1]() {
        int val;
        while (q.pop(val)) { sum1 += val; cnt1++; }
    });
    std::thread c2([&q, &sum2, &cnt2]() {
        int val;
        while (q.pop(val)) { sum2 += val; cnt2++; }
    });

    producer.join();
    c1.join();
    c2.join();

    int totalCnt = cnt1 + cnt2;
    int totalSum = sum1 + sum2;
    EXPECT_EQ(totalCnt, N);
    EXPECT_EQ(totalSum, N * (N - 1) / 2);
}
