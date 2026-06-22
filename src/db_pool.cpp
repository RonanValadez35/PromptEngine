#include "db_pool.h"

namespace {

// Fold connection-level timeouts into the URL so every connection (initial and
// reconnect) is protected against hanging forever on a dead/slow server:
//   connect_timeout      - cap connect/reconnect time
//   keepalives*/tcp_*    - detect a silently dead TCP connection
//   statement_timeout    - server-side cap so a query can't run forever (ms)
// %20 = space, %3D = '=' (percent-encoded for the URI query string).
std::string withTimeouts(const std::string& db_url) {
    static const std::string params =
        "connect_timeout=10"
        "&keepalives=1"
        "&keepalives_idle=30"
        "&keepalives_interval=10"
        "&tcp_user_timeout=60000"
        "&options=-c%20statement_timeout%3D120000";

    const char separator = (db_url.find('?') == std::string::npos) ? '?' : '&';
    return db_url + separator + params;
}

}

DBPool::DBPool(const std::string& db_url, size_t num_connections) : m_db_url(withTimeouts(db_url)) {
    for (size_t i = 0; i < num_connections; i++) {
        m_connection_pool.push(std::make_unique<pqxx::connection>(m_db_url));
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