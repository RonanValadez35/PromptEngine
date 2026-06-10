#include "thread_pool.h"
#include "ollama_runner.h"

ThreadPool::ThreadPool(TSQueue& queue, int numThreads):
    m_jobsQueue(queue) {
        if (numThreads > std::thread::hardware_concurrency() - 2) {
            throw std::runtime_error("Too many threads requested");
        }
        for (size_t i = 0; i < numThreads; i++) {
            m_threads.emplace_back([this](){
                OllamaRunner ollamaClient;
                while (true) {

                Job popedJob = std::move(m_jobsQueue.pop());
                if (popedJob.jobId == -1) {
                    break;
                }
                try{
                    popedJob.jobState->status = PROCESSING;
                    std::string response = ollamaClient.getResponse(popedJob.message);
                    popedJob.jobState->ollamaResponse = std::move(response);
                    popedJob.jobState->status = COMPLETED;
                } catch(const std::exception& e) {
                    popedJob.jobState->errorMessage = e.what();
                    popedJob.jobState->status = FAILED;
                }

            }
            });
        }
}
ThreadPool::~ThreadPool() {

    for (size_t i = 0; i < m_threads.size(); i++) {
        Job shutdownJob{-1, "__shutdown__", nullptr};

        m_jobsQueue.push(std::move(shutdownJob));
    }

    for (auto& thread: m_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}
