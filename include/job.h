#pragma once
#include <string>
#include <future>

enum statuses {QUEUED, PROCESSING, COMPLETED, FAILED};

/*For now the job will have an ID, message, status and result
    time stamp will be added later*/
struct Job {
    int jobId;
    std::string message;
    std::promise<std::string> ollamaResponse;
    statuses status;
    
};