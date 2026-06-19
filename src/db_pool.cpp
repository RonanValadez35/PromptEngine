#include "db_pool.h"

DBPool::DBPool(const std::string& db_url, size_t num_connections) : m_db_url(db_url) {
    for (size_t i = 0; i < num_connections; i++) {
        m_connection_pool.push(std::make_unique<pqxx::connection>(db_url));
    }
}

DBPool::ConnectionPtr DBPool::acquire() {
    std::unique_lock<std::mutex> lock(m_mutex);

    m_condv.wait(lock, [this]() {return !m_connection_pool.empty();});

    auto popped_connection = std::move(m_connection_pool.front());
    m_connection_pool.pop();

    return DBPool::ConnectionPtr(popped_connection.release(), [this](pqxx::connection* connection) {
        checkin(connection);
    });
}

void DBPool::checkin(pqxx::connection* connection) {
    std::unique_ptr<pqxx::connection> to_return;

    if (connection->is_open()) {
        to_return = std::unique_ptr<pqxx::connection>(connection);
    } else {
        delete connection;
        try {
            to_return = std::make_unique<pqxx::connection>(m_db_url);
        } catch (const std::exception&) {
            to_return = nullptr;
        }
    }

    if (to_return) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_connection_pool.push(std::move(to_return));
        }
        m_condv.notify_one();
    }
}