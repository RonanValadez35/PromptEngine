#include "ollama_runner.h"

#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <iostream>

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    const size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

std::string jsonEscape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());

    for (char c : text) {
        switch (c) {
            case '"':  escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:   escaped += c; break;
        }
    }

    return escaped;
}

std::string extractOllamaResponse(std::string_view json) {
    constexpr std::string_view key = R"("content":")";
    const size_t start = json.find(key);
    if (start == std::string_view::npos) {
        throw std::runtime_error("Missing response field in Ollama response");
    }

    std::string value;
    for (size_t i = start + key.size(); i < json.size(); ++i) {
        const char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            switch (json[++i]) {
                case '"':  value += '"'; break;
                case '\\': value += '\\'; break;
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                default:   value += json[i]; break;
            }
        } else if (c == '"') {
            return value;
        } else {
            value += c;
        }
    }

    throw std::runtime_error("Unterminated response field in Ollama response");
}

}  // namespace

OllamaRunner::OllamaRunner(std::string model) : m_model(std::move(model)) {
    m_curl = curl_easy_init();

    if (!m_curl) {
        throw std::runtime_error("Failed to initialize curl");
    }
}

OllamaRunner::~OllamaRunner() {
    curl_easy_cleanup(m_curl);
}

std::string OllamaRunner::getResponse(std::string_view prompt) {
    std::cout << "Prompt: " << prompt << std::endl;
    const std::string body =
        R"({"model":")" + jsonEscape(m_model) + R"(","messages":[{"role":"user","content":")" +
        jsonEscape(prompt) + R"("}],"stream":false})";
    std::cout << "Body: " << body << std::endl;
    const char* apiKey = std::getenv("OLLAMA_KEY");
    if (!apiKey || *apiKey == '\0') {
        throw std::runtime_error("OLLAMA_KEY environment variable not set");
    }
    const std::string authHeader = "Authorization: Bearer " + std::string(apiKey);

    std::string response;
    curl_slist* headers = nullptr;

    curl_easy_reset(m_curl);
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, authHeader.c_str());


    curl_easy_setopt(m_curl, CURLOPT_URL, "https://ollama.com/api/chat");
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response);

    const CURLcode result = curl_easy_perform(m_curl);
    std::cout << "Response: " << response << std::endl;
    curl_slist_free_all(headers);

    if (result != CURLE_OK) {
        std::cout << "Error: " << curl_easy_strerror(result) << std::endl;
        throw std::runtime_error(curl_easy_strerror(result));
    }

    long statusCode = 0;
    curl_easy_getinfo(m_curl, CURLINFO_RESPONSE_CODE, &statusCode);
    
    if (statusCode < 200 || statusCode >= 300) {
        std::cout << "HTTP Status: " << statusCode << '\n';
        std::cout << "Response Body:\n" << response << '\n';
    
        throw std::runtime_error(
            "Ollama request failed with HTTP status " +
            std::to_string(statusCode)
        );
    }

    return extractOllamaResponse(response);
}
