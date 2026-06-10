#include "crow.h"
#include "job.h"
#include "ts_queue.h"
#include "thread_pool.h"
#include "job_registry.h"
#include <atomic>

int main() {
    crow::SimpleApp app;

    TSQueue jobsQueue;
    ThreadPool pool(jobsQueue, 4);
    std::atomic<int> nextJobId{0};
    JobRegistry jobMap;



    CROW_ROUTE(app, "/generate").methods(crow::HTTPMethod::POST)
    ([&jobsQueue, &nextJobId, &jobMap](const crow::request& req){
        const auto body = crow::json::load(req.body);

        if(!body) {
            return crow::response(400, "Invalid JSON input");
        }

        auto jStatePointer = std::make_shared<JobState>();
        jStatePointer->status = QUEUED;

        Job job;
        int id = nextJobId++;
        job.jobId = id;
        job.message = body["prompt"].s();
        
        job.jobState = jStatePointer;
        jobMap.insertRegistry(job.jobId, jStatePointer);
        jobsQueue.push(std::move(job));
        

        try {
            return crow::response(200, std::to_string(id));
        } catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
    });

    CROW_ROUTE(app, "/job/<int>").methods(crow::HTTPMethod::GET)
    ([&jobMap](int jobId){
        auto jobPointer = jobMap.getRegistry(jobId);
        std::string response;
        if (!jobPointer) {
            response = "Job not found in registry";
            return crow::response(404, response);
        }
        const statuses s = jobPointer->status;
        if (s == FAILED) {
            response = jobPointer->errorMessage;
            return crow::response(500, response);
        }
        if (s == PROCESSING || s == QUEUED) {
            response = "Job still processing";
            return crow::response(202, response);
        } else if (s == COMPLETED) {
            return crow::response(200, jobPointer->ollamaResponse);
        } else {
            return crow::response(500, "Unknown job state");
        }
    });

    app.port(18080).run();


}