#include <gtest/gtest.h>
#include "thread_pool.h"
#include <future>
#include <chrono>
#include <thread>

TEST(ThreadPoolTests, CheckShutDown) {
    TSQueue queue;

    {
        ThreadPool workers(queue, 4);

        Job j1{1, "", std::promise<std::string>{}, QUEUED};
        Job j2{2, "",  std::promise<std::string>{}, QUEUED};
        Job j3{3, "",  std::promise<std::string>{}, QUEUED};
        Job j4{4, "",  std::promise<std::string>{}, QUEUED};

        queue.push(std::move(j1));
        queue.push(std::move(j2));
        queue.push(std::move(j3));
        queue.push(std::move(j4));

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    SUCCEED();
}

TEST(ThreadPoolTests, CheckPromiseRetrieval) {
    TSQueue queue;
    {
        ThreadPool workers(queue, 1);

        std::promise<std::string> promise;
        std::future<std::string> future = promise.get_future();
        Job j1{1, "", std::move(promise), QUEUED};
        queue.push(std::move(j1));

        std::string response = future.get();

        EXPECT_EQ(response, "Dummy response");

    }
    SUCCEED();
}