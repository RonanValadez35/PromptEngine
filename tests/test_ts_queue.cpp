#include <thread>
#include <condition_variable>
#include "ts_queue.h"
#include <chrono>
#include <cassert>
#include <iostream>
#include <gtest/gtest.h>

TEST(TSQueueTests, ConsumerProducerTest) {

    Job newJob{1, "this is a message", "response", QUEUED};
    Job receivedJob{0, "", "", FAILED};
    TSQueue queue;
    std::thread consumer([&]() {
        receivedJob = queue.pop();
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::thread producer([&]() {
        queue.push(newJob);
    });

    consumer.join();
    producer.join();
    EXPECT_EQ(receivedJob.jobId, 1);
    EXPECT_EQ(receivedJob.message, "this is a message");
    EXPECT_EQ(receivedJob.ollamaResponse, "response");
    EXPECT_EQ(receivedJob.status, QUEUED);

}