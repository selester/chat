#include "json.hpp"
#include "message.hpp"
#include "time_utils.hpp"

#include <algorithm>
#include <iostream>

#include <httplib.h>

int main(int argc, const char** argv) {
    if (argc != 2) {
        std::cout << "Usage: " << argv[0] << " <port>\n\n";
        return 1;
    }
    int port = std::stoi(argv[1]);
    httplib::Server s;
    std::vector<Message> message_queue;
    std::mutex message_queue_lock;
    s.Post("/chat", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            Message m = Message::from_json_string(req.body);
            std::cout << "New message from " << m.username << std::endl;
            {
                std::scoped_lock<std::mutex> lock{message_queue_lock};
                message_queue.push_back(m);
                if (message_queue.size() > 1000) {
                    std::sort(message_queue.begin(), message_queue.end());
                    message_queue.erase(message_queue.begin(), message_queue.begin() + 200);
                }
            }
            res.status = httplib::StatusCode::OK_200;
        } catch (std::exception& e) {
            std::cout << "Error receiving new message: " << e.what() << std::endl;
            res.status = httplib::StatusCode::InternalServerError_500;
        } catch (...) {
            std::cout << "Error receiving new message" << std::endl;
            res.status = httplib::StatusCode::InternalServerError_500;
        }
    });
    s.Get("/chat", [&](const httplib::Request& req, httplib::Response& res) {
        try {
            nlohmann::json new_messages;
            if (req.has_param("last_update_time")) {
                const auto last_time = time_from_string(req.get_param_value("last_update_time"));

                std::scoped_lock<std::mutex> lock{message_queue_lock};
                for (const auto& m : message_queue) {
                    if (m.timestamp > last_time) {
                        new_messages.push_back(m.to_json());
                    }
                }
            } else {
                std::scoped_lock<std::mutex> lock{message_queue_lock};
                for (const auto& m : message_queue) {
                    new_messages.push_back(m.to_json());
                }
            }
            res.set_content(to_string(new_messages), "application/json");
            res.status = httplib::StatusCode::OK_200;

        } catch (std::exception& e) {
            std::cout << "Error receiving new message: " << e.what() << std::endl;
            res.status = httplib::StatusCode::InternalServerError_500;
        } catch (...) {
            std::cout << "Error receiving new message" << std::endl;
            res.status = httplib::StatusCode::InternalServerError_500;
        }
    });
    std::cout << "Server listening on port " << port << "\n";
    s.listen("0.0.0.0", port);
}
