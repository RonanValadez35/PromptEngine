#pragma once
#include <curl/curl.h>
#include <string>
#include <string_view>
class OllamaRunner {
private:
    CURL *m_curl;
    std::string m_model;

public:
    explicit OllamaRunner(std::string model = "gemma4:31b-cloud");
    ~OllamaRunner();
    OllamaRunner(const OllamaRunner&) = delete;
    OllamaRunner& operator=(const OllamaRunner&) = delete;
    OllamaRunner(OllamaRunner&&) = delete;
    OllamaRunner& operator=(OllamaRunner&&) = delete;
    std::string getResponse(std::string_view prompt);

};