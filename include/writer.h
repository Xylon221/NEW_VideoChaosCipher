#ifndef WRITER_H
#define WRITER_H

#include "SafeQueue.h"
#include "vpu_io.h"
#include "stats.h"

void writerThread(VPUEncoder &writer, SafeQueue<FrameData> &writeQueue,
                  BenchStats &stats);

#endif
