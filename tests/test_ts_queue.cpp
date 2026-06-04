#include <thread>
#include <condition_variable>
#include "ts_queue.h"
#include <chrono>
#include <cassert>
#include <iostream>
#include <gtest/gtest.h>
#include <utility>

TEST(TSQueueTests, ConsumerProducerTest) {

    Job newJob{1, "this is a message", std::promise<std::string>{}, QUEUED};

    std::future<std::string> future = newJob.ollamaResponse.get_future();
    Job receivedJob{0, "", std::promise<std::string>{}, FAILED};
    TSQueue queue;
    std::thread consumer([&]() {
        receivedJob = queue.pop();
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::thread producer([&]() {
        queue.push(std::move(newJob));
    });

    consumer.join();
    producer.join();

    receivedJob.ollamaResponse.set_value("response");

    EXPECT_EQ(receivedJob.jobId, 1);
    EXPECT_EQ(receivedJob.message, "this is a message");
    EXPECT_EQ(future.get(), "response");
    EXPECT_EQ(receivedJob.status, QUEUED);

}