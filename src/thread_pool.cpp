#include "thread_pool.h"
#include "ollama_runner.h"
#include <chrono>

ThreadPool::ThreadPool(JobStore& jobStore, int numThreads):
    m_jobStore(jobStore) {
        if (numThreads > std::thread::hardware_concurrency() - 2) {
            throw std::runtime_error("Too many threads requested");
        }
        for (size_t i = 0; i < numThreads; i++) {
            m_threads.emplace_back([this](){
                OllamaRunner ollamaClient;
                while (m_runningFlag.load()) {

                    auto popedJob = m_jobStore.claimNextJob();
                    if (!popedJob) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        continue;
                    }
                    try{
                        std::string response = ollamaClient.getResponse(popedJob->prompt);
                        m_jobStore.completeJob(popedJob->jobId, response);
                    } catch(const std::exception& e) {
                        m_jobStore.failedJob(popedJob->jobId, e.what());
                    }
                }

            });
        }
}
ThreadPool::~ThreadPool() {
    m_runningFlag.store(false);

    for (auto& thread: m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
