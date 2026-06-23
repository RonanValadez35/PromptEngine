#include "crow.h"
#include "job.h"
#include "thread_pool.h"
#include "db_pool.h"
#include "job_store.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <cstdlib>

int main() {
    crow::SimpleApp app;

    // new Code for database:
    const char* db_url = std::getenv("DATABASE_URL");
    if (!db_url) {
        throw std::runtime_error("DATABASE_URL not set");
    }
    DBPool dbPool(db_url, 8);
    JobStore jobStore(dbPool);

    ThreadPool pool(jobStore, 4);

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
    ([&jobStore](const crow::request& req){
        const auto body = crow::json::load(req.body);

        if(!body) {
            return crow::response(400, "Invalid JSON input");
        }        

        try {
            int jobId = jobStore.insertJob(body["prompt"].s());
            return crow::response(200, std::to_string(jobId));
        } catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
    });

    CROW_ROUTE(app, "/job/<int>").methods(crow::HTTPMethod::GET)
    ([&jobStore](int jobId){
        auto job = jobStore.getJob(jobId);
        std::string response;
        if (!job) {
            response = "Job not found in registry";
            return crow::response(404, response);
        }
        const statuses s = job->status;
        if (s == FAILED) {
            response = job->errorMessage;
            return crow::response(500, response);
        }
        if (s == PROCESSING || s == QUEUED) {
            response = "Job still processing";
            return crow::response(202, response);
        } else if (s == COMPLETED) {
            return crow::response(200, job->ollamaResponse);
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