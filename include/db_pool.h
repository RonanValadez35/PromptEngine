#pragma once
#include <pqxx/pqxx>
#include <mutex>
#include <queue>
#include <memory>
#include <condition_variable>
#include <functional>
#include <string>

class DBPool {
public:
    using ConnectionPtr = std::unique_ptr<pqxx::connection, std::function<void(pqxx::connection*)>>;
    ConnectionPtr acquire();
    DBPool(const std::string& db_url, size_t num_connections);

private:
    void checkin(pqxx::connection* connection);
    std::string m_db_url;
    std::queue<std::unique_ptr<pqxx::connection>> m_connection_pool;
    std::mutex m_mutex;
    std::condition_variable m_condv;
    

};