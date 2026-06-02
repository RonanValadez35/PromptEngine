#pragma once
#include "job.h"
#include <queue>
#include <mutex>
#include <condition_variable>

class TSQueue {
private:
    std::queue<Job> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condv;

public:
    void push(const Job& newJob);
    Job pop();

};