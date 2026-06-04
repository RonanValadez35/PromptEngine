#include "ts_queue.h"

void TSQueue::push(Job&& newJob) {
    std::unique_lock<std::mutex> lock(m_mutex);

    m_queue.push(std::move(newJob));

    m_condv.notify_one(); 
}

Job TSQueue::pop() {
    std::unique_lock<std::mutex> lock(m_mutex);

    m_condv.wait(lock, [this]() {return !m_queue.empty();});

    Job poppedJob = std::move(m_queue.front());
    m_queue.pop();

    return poppedJob;
}

