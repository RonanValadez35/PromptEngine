#include "job.h"
#include <queue>
#include <mutex>
#include <condition_variable>

class ts_queue {
private:
    std::queue<Job> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_condv;

public:
    void push(const Job& newJob) {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_queue.push(newJob);

        m_condv.notify_one(); 
    }

    Job pop() {
        std::unique_lock<std::mutex> lock(m_mutex);

        m_condv.wait(lock, [this]() {return !m_queue.empty();});

        Job poppedJob = m_queue.front();
        m_queue.pop();

        return poppedJob;
    }

};