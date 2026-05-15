#ifndef READER_H
#define READER_H

#include "SafeQueue.h"
#include "vpu_io.h"
#include "stats.h"

void readerThread(VPUDecoder &dec, SafeQueue<FrameData> &readQueue,
                  BenchStats &stats);

#endif
