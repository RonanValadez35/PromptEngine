#include "ollama_runner.h"

#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <nlohmann/json.hpp>
#include <iostream>

namespace {

size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
    const size_t totalSize = size * nmemb;
    response->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

}

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

    nlohmann::json  bodyJson = {
        {"model", m_model},
        {"messages", {{
            {"role", "user"},
            {"content", prompt}
        }}},
        {"stream", false}
    };

    const std::string body = bodyJson.dump();
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
    curl_easy_setopt(m_curl, CURLOPT_POSTFIELDSIZE, body.size());
    curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &response);

    curl_easy_setopt(m_curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(m_curl, CURLOPT_TIMEOUT, 150L);

    const CURLcode result = curl_easy_perform(m_curl);
    std::cout << "Response: " << response << std::endl;

    curl_slist_free_all(headers);

    if (result != CURLE_OK) {
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

    nlohmann::json responseJson = nlohmann::json::parse(response);

    if (!responseJson.contains("message") ||
        !responseJson["message"].contains("content")) {
        throw std::runtime_error("Missing message.content in Ollama response");
    }

    return responseJson["message"]["content"].get<std::string>();
}
