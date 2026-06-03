#include "thread_pool.h"

ThreadPool::ThreadPool(TSQueue& queue, int numThreads):
    m_jobsQueue(queue) {
        if (numThreads > std::thread::hardware_concurrency() - 2) {
            throw std::runtime_error("Too many threads requested");
        }
        for (size_t i = 0; i < numThreads; i++) {
            m_threads.emplace_back([this](){
                while (true) {

                Job popedJob = m_jobsQueue.pop();
                if (popedJob.jobId == -1) {
                    break;
                }

                popedJob.ollamaResponse = "Dummy response";
                popedJob.status = COMPLETED;
            }
            });
        }
    }
ThreadPool::~ThreadPool() {
    Job shutdownJob {-1, "__shutdown__", "", FAILED};

    for (size_t i = 0; i < m_threads.size(); i++) {
        m_jobsQueue.push(shutdownJob);
    }

    for (auto& thread: m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
