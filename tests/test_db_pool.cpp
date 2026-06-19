#include "db_pool.h"
#include <gtest/gtest.h>
#include <thread>

TEST(testDBPool, checkConnection) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool connection_pool(db_url, 4);
    std::thread t1([&connection_pool]() {
        auto conn = connection_pool.acquire();
        pqxx::work tx(*conn);
        pqxx::result res = tx.exec("INSERT INTO jobs (prompt) VALUES ('test prompt') RETURNING id");
        tx.commit();
        EXPECT_EQ(res.size(), 1);
        

    });
    t1.join();
}

TEST(testDBPool, checkThreadSafetyEasyCase) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    DBPool connection_pool(db_url, 1);

    std::thread t1([&connection_pool]() {
        auto conn = connection_pool.acquire();
        pqxx::work tx(*conn);
        pqxx::result res = tx.exec("INSERT INTO jobs (prompt) VALUES ('test prompt') RETURNING id");
        tx.commit();
        EXPECT_EQ(res.size(), 1);
    });

    std::thread t2([&connection_pool]() {
        auto conn = connection_pool.acquire();
        pqxx::work tx(*conn);
        pqxx::result res = tx.exec("INSERT INTO jobs (prompt) VALUES ('Random Prompt') RETURNING id");
        tx.commit();
        EXPECT_EQ(res.size(), 1);
    });
    t1.join();
    t2.join();
}

TEST(testDBPool, manyThreadsSharePool) {
    const char* db_url = std::getenv("DATABASE_URL");
    ASSERT_NE(db_url, nullptr) << "DATABASE_URL not set";

    constexpr int kThreads = 8;
    DBPool pool(db_url, 3);

    std::vector<std::thread> threads;
    for (int i = 0; i < kThreads; i++) {
        threads.emplace_back([&pool]() {
            auto conn = pool.acquire();
            pqxx::work tx(*conn);
            tx.exec("INSERT INTO jobs (prompt) VALUES ('stress')");
            tx.commit();
        });
    }
    for (auto& t : threads) t.join();

    auto conn = pool.acquire();
    pqxx::work tx(*conn);
    pqxx::result res = tx.exec("SELECT COUNT(*) FROM jobs WHERE prompt = 'stress'");
    EXPECT_EQ(res[0][0].as<int>(), kThreads);
}