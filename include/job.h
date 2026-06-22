#pragma once
#include <string>
#include <memory>
#include <atomic>

enum statuses {QUEUED, PROCESSING, COMPLETED, FAILED};

// struct JobState {
//     std::atomic<statuses> status;
//     std::string ollamaResponse;   
//     std::string errorMessage;     //set upon failure
// };

// struct Job {
//     int jobId;
//     std::string message;
//     std::shared_ptr<JobState> jobState;
// };

struct JobRecord {
    int jobId;
    std::string prompt;
    std::string ollamaResponse;
    statuses status;
    std::string errorMessage;
};