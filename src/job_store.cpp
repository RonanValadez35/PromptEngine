#include "job_store.h"

JobStore::JobStore(DBPool& dbPool): m_dbPool(dbPool){}

int JobStore::insertJob(const std::string& prompt) {
    DBPool::ConnectionPtr conn = m_dbPool.acquire();
    pqxx::work tx(*conn);
    pqxx::result res = tx.exec(
        "INSERT INTO jobs (prompt) VALUES ($1) RETURNING id",
        pqxx::params{prompt}
    );

    int returnedId = res[0]["id"].as<int>();
    tx.commit();
    return returnedId;
}

std::optional<JobRecord> JobStore::claimNextJob() {
    DBPool::ConnectionPtr conn = m_dbPool.acquire();
    pqxx::work tx(*conn);

    pqxx::result res = tx.exec(
        R"(UPDATE jobs SET status='PROCESSING', locked_at=now(), updated_at=now()
           WHERE id = (
               SELECT id FROM jobs WHERE status='QUEUED'
               ORDER BY created_at
               FOR UPDATE SKIP LOCKED
               LIMIT 1
           )
           RETURNING id, prompt)"
    );

    tx.commit();

    if (res.empty()) {
        return std::nullopt;
    }

    JobRecord record;
    record.jobId = res[0]["id"].as<int>();
    record.prompt = res[0]["prompt"].as<std::string>();
    return record;
}

std::optional<JobRecord> JobStore::getJob(int id) {
    DBPool::ConnectionPtr conn = m_dbPool.acquire();
    pqxx::work tx(*conn);

    pqxx::result res = tx.exec(
        R"(SELECT prompt, status, ollama_response, error FROM jobs WHERE id = $1)",
        pqxx::params{id}
    );

    tx.commit();

    if (res.empty()) {
        return std::nullopt;
    }

    const auto r = res[0];

    JobRecord record;
    record.jobId = id;
    record.prompt = r["prompt"].as<std::string>();

    const std::string s = r["status"].as<std::string>();

    if (s == "QUEUED") {
        record.status = QUEUED;
    } else if (s == "PROCESSING"){
        record.status = PROCESSING;
    } else if (s == "COMPLETED") {
        record.status = COMPLETED;
    } else {
        record.status = FAILED;
    }

    if (!r["ollama_response"].is_null()) {
        record.ollamaResponse = r["ollama_response"].as<std::string>();
    }

    if (!r["error"].is_null()) {
        record.errorMessage = r["error"].as<std::string>();
    }

    return record;
}

void JobStore::completeJob(int id, const std::string& result) {
    DBPool::ConnectionPtr conn = m_dbPool.acquire();
    pqxx::work tx(*conn);

    tx.exec(
        R"(UPDATE jobs SET status='COMPLETED', ollama_response=$2, updated_at=now()
           WHERE id=$1)",
        pqxx::params{id, result}
    );

    tx.commit();
}

void JobStore::failedJob(int id, const std::string& failureResponse) {
    DBPool::ConnectionPtr conn = m_dbPool.acquire();
    pqxx::work tx(*conn);

    tx.exec(
        R"(UPDATE jobs SET status='FAILED', error=$2, updated_at=now()
           WHERE id=$1)",
        pqxx::params{id, failureResponse}
    );

    tx.commit();
}

void JobStore::requeueJobs() {
    constexpr int maxStaleMinutes = 4;
    constexpr int maxAttempts = 3;

    DBPool::ConnectionPtr conn = m_dbPool.acquire();
    pqxx::work tx(*conn);

    pqxx::result res = tx.exec(
        R"(UPDATE jobs
           SET status='QUEUED',
               locked_at=NULL,
               error=NULL,
               attempts=attempts+1,
               updated_at=now()
           WHERE (status='PROCESSING' AND locked_at < now() - make_interval(mins => $1))
              OR (status='FAILED' AND attempts < $2))",
        pqxx::params{maxStaleMinutes, maxAttempts}
    );

    tx.commit();
}