#pragma once

#include <thread>
#include <vector>
#include "ts_queue.h"

class ThreadPool {
private:
    std::vector<std::thread>  m_threads;
    TSQueue& m_jobsQueue;
    
public:
    ThreadPool(TSQueue& queue, int numThreads);
    ~ThreadPool();
};