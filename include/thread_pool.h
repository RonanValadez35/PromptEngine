#pragma once

#include <thread>
#include <vector>
#include <atomic>
#include "job_store.h"

class ThreadPool {
private:
std::atomic<bool> m_runningFlag{true};
    std::vector<std::thread>  m_threads;
    JobStore& m_jobStore;
    
public:
    ThreadPool(JobStore& jobStore, int numThreads);
    ~ThreadPool();
};