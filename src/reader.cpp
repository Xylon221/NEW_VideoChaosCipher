#include "reader.h"
#include <iostream>

void readerThread(VPUDecoder &dec, SafeQueue<FrameData> &readQueue,
                   BenchStats &stats) {
    std::cout << "Read started." << std::endl;
    int index = 0;
    while (true) {
        FrameData data;
        if (!dec.read(data.frame)) {
            break;
        }
        data.frame_index = index++;

        int d = ++stats.readQueueDepth;
        if (d > stats.readQueuePeak.load()) {
            stats.readQueuePeak.store(d);
        }

        readQueue.push(data);
    }
    readQueue.setFinished();
    std::cout << "Read finished, totally read " << index << " frame" << std::endl;
}
