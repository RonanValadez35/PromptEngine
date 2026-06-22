#pragma once
#include "db_pool.h"
#include "job.h"
#include <optional>

class JobStore {
public:
    JobStore(DBPool& dbPool);
    int insertJob(const std::string& prompt);
    std::optional<JobRecord> claimNextJob();
    std::optional<JobRecord> getJob(int id);
    void completeJob(int id, const std::string& result);
    void failedJob(int id, const std::string& failureResponse);
    // requeue could be called on a timer and pass in a time and if the difference is greater than 3 mins switch to queue.
    // or if failed.
    void requeueJobs(); 

    

private:
    DBPool& m_dbPool;
};