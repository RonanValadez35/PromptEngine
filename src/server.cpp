#include "crow.h"
#include "job.h"
#include "ts_queue.h"
#include "thread_pool.h"
#include <atomic>

int main() {
    crow::SimpleApp app;

    TSQueue jobsQueue;
    ThreadPool pool(jobsQueue, 4);
    std::atomic<int> nextJobId{0};

    CROW_ROUTE(app, "/generate").methods(crow::HTTPMethod::POST)
    ([&jobsQueue, &nextJobId](const crow::request& req){
        const auto body = crow::json::load(req.body);

        if(!body) {
            return crow::response(400, "Invalid JSON input");
        }

        Job job;
        job.jobId = nextJobId++;
        job.message = body["prompt"].s();
        job.status = QUEUED;

        std::future<std::string> resultFuture = job.ollamaResponse.get_future();
        jobsQueue.push(std::move(job));

        try {
            std::string result = resultFuture.get();
            return crow::response(200, result);
        } catch (const std::exception& e) {
            return crow::response(500, e.what());
        }
    });

    app.port(18080).run();


}