#include "crow.h"
#include "job.h"
#include "ts_queue.h"
#include "thread_pool.h"
#include "job_registry.h"
#include "db_pool.h"
#include "job_store.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

int main() {
    crow::SimpleApp app;

    TSQueue jobsQueue;
    ThreadPool pool(jobsQueue, 4);
    std::atomic<int> nextJobId{0};
    JobRegistry jobMap;


    // new Code for database:
    const char* db_url = std::getenv("DATABASE_URL");
    if (!db_url) {
        throw std::runtime_error("DATABASE_URL not set");
    }
    DBPool dbPool(db_url, 8);
    JobStore jobStore(dbPool);

    // new code for reaper thread:
    std::atomic<bool> stopFlag{false};
    std::condition_variable reaperCV;
    std::mutex reaperMutex;

    std::thread reaperThread([&](){
        constexpr auto interval = std::chrono::seconds(30);

        std::unique_lock<std::mutex> lock(reaperMutex);
        while (!stopFlag.load()) {

            if (reaperCV.wait_for(lock, interval, [&](){return stopFlag.load();})) {
                break;
            }
            lock.unlock();
            try {
                jobStore.requeueJobs();
            } catch (const std::exception& e) {
                std::cerr << "Reaper thread error: " << e.what() << std::endl;
            }
            lock.lock();
        }

    });


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
    {
        std::lock_guard<std::mutex> lk(reaperMutex);
        stopFlag.store(true);
    }
    reaperCV.notify_all();
    reaperThread.join();


}