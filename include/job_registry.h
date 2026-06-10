#pragma once
#include <unordered_map>
#include <memory>
#include "job.h"
#include <mutex>

class JobRegistry {
private: 
    std::unordered_map<int, std::shared_ptr<JobState>> m_jobStates;
    std::mutex m_mutex;

public:
    std::shared_ptr<JobState> getRegistry(int jobId);
    void insertRegistry(int jobId, std::shared_ptr<JobState> jState);
};