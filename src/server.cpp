#include "crow.h"

int main() {
    crow::SimpleApp app;
    CROW_ROUTE(app, "/")([](){
        return "Hello World";
    });

    CROW_ROUTE(app, "/generate").methods(crow::HTTPMethod::POST)([](const crow::request& req){
        const auto body = crow::json::load(req.body);

        if(!body) {
            return crow::response(400, "Invalid JSON input");
        }

        std::string prompt = body["prompt"].s();
        std::cout << prompt << std::endl;
        return crow::response(200, "Prompt recieved");
    });



    app.port(18080).run();


}