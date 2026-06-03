#include <gtest/gtest.h>
#include "thread_pool.h"

#include <chrono>
#include <thread>

TEST(ThreadPoolTests, CheckShutDown)
{
    TSQueue queue;

    {
        ThreadPool workers(queue, 4);

        Job j1{1, "", "", QUEUED};
        Job j2{2, "", "", QUEUED};
        Job j3{3, "", "", QUEUED};
        Job j4{4, "", "", QUEUED};

        queue.push(j1);
        queue.push(j2);
        queue.push(j3);
        queue.push(j4);

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    SUCCEED();
}