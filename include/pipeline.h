#ifndef PIPELINE_H
#define PIPELINE_H

#include <string>
#include "SafeQueue.h"
#include "stats.h"

void encryptThread(SafeQueue<FrameData> &readQueue,
                   SafeQueue<FrameData> &writeQueue,
                   int start_idx, int end_idx, float seed, int threadId,
                   BenchStats &stats);

bool processVideo(const std::string &input_path, const std::string &output_path,
                  float start_sec, float end_sec, float seed, int numThreads);

#endif
