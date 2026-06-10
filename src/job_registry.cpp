#include "job_registry.h"

void JobRegistry::insertRegistry(int jobId, std::shared_ptr<JobState> jState) {
    std::unique_lock<std::mutex> lock(m_mutex);
    m_jobStates.insert({jobId, std::move(jState)});
}

std::shared_ptr<JobState> JobRegistry::getRegistry(int jobId) {
    std::unique_lock<std::mutex> lock(m_mutex);
    auto it = m_jobStates.find(jobId);
    if (it != m_jobStates.end()) {
        return it->second;
    }
    return nullptr;
}