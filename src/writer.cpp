#include "writer.h"
#include <iostream>
#include <map>

void writerThread(VPUEncoder &writer, SafeQueue<FrameData> &writeQueue,
                  BenchStats &stats) {
    std::cout << "Write started." << std::endl;

    FrameData data;
    int nextToWrite = 0;
    std::map<int, FrameData> reorderBuffer;
    int index = 0;

    while (writeQueue.pop(data)) {
        --stats.writeQueueDepth;

        reorderBuffer[data.frame_index] = std::move(data);

        while (!reorderBuffer.empty() &&
               reorderBuffer.begin()->first == nextToWrite) {
            writer.write(reorderBuffer.begin()->second.frame);
            reorderBuffer.erase(reorderBuffer.begin());
            nextToWrite++;
            index++;
        }
    }

    for (auto &pair : reorderBuffer) {
        writer.write(pair.second.frame);
        index++;
    }

    std::cout << "Write finished, totally write " << index << " frame" << std::endl;
}
