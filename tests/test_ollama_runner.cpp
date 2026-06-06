#include "ollama_runner.h"

#include <iostream>
#include <string>

int main() {
    OllamaRunner runner;

    const std::string prompt = "Whats your name";
    std::cout << "Sending prompt: " << prompt << std::endl;

    try {
        const std::string response = runner.getResponse(prompt);
        std::cout << "Ollama response:\n" << response << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
