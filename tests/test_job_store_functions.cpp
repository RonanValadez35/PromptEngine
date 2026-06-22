#include "job_store.h"
#include <gtest/gtest.h>
#include <thread>

namespace {
    void ensureJobsSchema(DBPool& pool) {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        tx.exec(
            "ALTER TABLE jobs ADD COLUMN IF NOT EXISTS attempts INT NOT NULL DEFAULT 0"
        );
        tx.commit();
    }

    void clearJobsTable(DBPool& pool) {
        ensureJobsSchema(pool);
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        tx.exec("TRUNCATE TABLE jobs RESTART IDENTITY");
        tx.commit();
    }
}

TEST(testJobStoreFunctions, checkInsertJob) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool connection_pool(db_url, 4);
    clearJobsTable(connection_pool);
    JobStore jobStore(connection_pool);
    std::thread t1([&jobStore]() {
        int id = jobStore.insertJob("This is checking insert");
        auto job = jobStore.getJob(id);
        EXPECT_EQ(job->prompt, "This is checking insert");
        EXPECT_EQ(job->status, QUEUED);
    });
    t1.join();
    clearJobsTable(connection_pool);
}

TEST(testJobStoreFunctions, claimNextJobSkipsLockedRow) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int firstId = jobStore.insertJob("first");
    int secondId = jobStore.insertJob("second");

    std::atomic<bool> lockHeld{false};
    std::atomic<bool> claimDone{false};
    std::optional<JobRecord> claimed;

    std::thread locker([&]() {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        tx.exec(
            "SELECT id FROM jobs WHERE status='QUEUED' ORDER BY created_at FOR UPDATE LIMIT 1"
        );
        lockHeld = true;
        while (!claimDone) {}
        tx.commit();
    });

    while (!lockHeld) {}

    std::thread claimer([&]() {
        claimed = jobStore.claimNextJob();
        claimDone = true;
    });

    claimer.join();
    locker.join();

    ASSERT_TRUE(claimed.has_value());
    EXPECT_EQ(claimed->prompt, "second");

    auto first = jobStore.getJob(firstId);
    auto second = jobStore.getJob(secondId);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->status, QUEUED);
    EXPECT_EQ(second->status, PROCESSING);
    clearJobsTable(pool);
}

TEST(testJobStoreFunctions, completeJobSetsStatusAndResponse) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int id = jobStore.insertJob("complete me");
    jobStore.claimNextJob();
    jobStore.completeJob(id, "ollama output");

    auto job = jobStore.getJob(id);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, COMPLETED);
    EXPECT_EQ(job->ollamaResponse, "ollama output");
    EXPECT_TRUE(job->errorMessage.empty());

    clearJobsTable(pool);
}

TEST(testJobStoreFunctions, failedJobSetsStatusAndError) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int id = jobStore.insertJob("fail me");
    jobStore.claimNextJob();
    jobStore.failedJob(id, "timeout");

    auto job = jobStore.getJob(id);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, FAILED);
    EXPECT_EQ(job->errorMessage, "timeout");
    EXPECT_TRUE(job->ollamaResponse.empty());

    clearJobsTable(pool);
}

TEST(testJobStoreFunctions, requeueJobsRequeuesStaleProcessingJob) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int id = jobStore.insertJob("stale");
    jobStore.claimNextJob();

    {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        tx.exec(
            "UPDATE jobs SET locked_at = now() - interval '5 minutes' WHERE id = $1",
            pqxx::params{id}
        );
        tx.commit();
    }

    jobStore.requeueJobs();

    auto job = jobStore.getJob(id);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, QUEUED);
    EXPECT_TRUE(job->errorMessage.empty());

    {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        pqxx::result res = tx.exec(
            "SELECT locked_at, attempts FROM jobs WHERE id = $1",
            pqxx::params{id}
        );
        tx.commit();
        EXPECT_TRUE(res[0]["locked_at"].is_null());
        EXPECT_EQ(res[0]["attempts"].as<int>(), 1);
    }

    clearJobsTable(pool);
}

TEST(testJobStoreFunctions, requeueJobsSkipsRecentProcessingJob) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int id = jobStore.insertJob("fresh");
    jobStore.claimNextJob();

    jobStore.requeueJobs();

    auto job = jobStore.getJob(id);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, PROCESSING);

    clearJobsTable(pool);
}

TEST(testJobStoreFunctions, requeueJobsRequeuesFailedJobUnderMaxAttempts) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int id = jobStore.insertJob("retry");
    jobStore.failedJob(id, "transient error");

    jobStore.requeueJobs();

    auto job = jobStore.getJob(id);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, QUEUED);
    EXPECT_TRUE(job->errorMessage.empty());

    {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        pqxx::result res = tx.exec(
            "SELECT attempts FROM jobs WHERE id = $1",
            pqxx::params{id}
        );
        tx.commit();
        EXPECT_EQ(res[0]["attempts"].as<int>(), 1);
    }

    clearJobsTable(pool);
}

TEST(testJobStoreFunctions, requeueJobsSkipsFailedJobAtMaxAttempts) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool pool(db_url, 4);
    clearJobsTable(pool);
    JobStore jobStore(pool);

    int id = jobStore.insertJob("exhausted");

    {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        tx.exec(
            "UPDATE jobs SET status='FAILED', error='permanent', attempts=3 WHERE id = $1",
            pqxx::params{id}
        );
        tx.commit();
    }

    jobStore.requeueJobs();

    auto job = jobStore.getJob(id);
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->status, FAILED);
    EXPECT_EQ(job->errorMessage, "permanent");

    {
        auto conn = pool.acquire();
        pqxx::work tx(*conn);
        pqxx::result res = tx.exec(
            "SELECT attempts FROM jobs WHERE id = $1",
            pqxx::params{id}
        );
        tx.commit();
        EXPECT_EQ(res[0]["attempts"].as<int>(), 3);
    }

    clearJobsTable(pool);
}