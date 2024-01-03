#include "message.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/component_options.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <iostream>
#include <vector>

#include <httplib.h>

int main(int argc, const char** argv) {
    if (argc != 4) {
        std::cout << "Usage: " << argv[0] << " <host> <port> <username>\n\n";
        return 1;
    }

    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string user_name = argv[3];

    using namespace ftxui;

    httplib::Client cli{host, port};
    auto last_update_time = std::chrono::system_clock::time_point{std::chrono::seconds{0}};
    std::vector<Message> messages;
    std::mutex messages_mutex;
    std::string prompt;

    auto main_comp = Renderer([&] {
        Elements elems;
        {
            std::scoped_lock<std::mutex> lock{messages_mutex};
            std::transform(
                messages.begin(),
                messages.end(),
                std::back_inserter(elems),
                [](const Message& m) {
                    return hbox(text(std::string{"["} + time_to_human_readable_string(m.timestamp) +
                                     std::string{"] "}),
                                text(m.username),
                                text(": "),
                                text(m.msg));
                });
        }
        elems.push_back(separator());
        elems.push_back(text(prompt));
        return vbox(elems) | border;
    });

    main_comp |= CatchEvent([&](Event ev) {
        if (ev == Event::Custom) {
            return true;
        }
        if (ev.is_mouse()) {
            return true;
        }
        if (ev == Event::Return) {
            if (!prompt.empty()) {
                Message m{{user_name}, std::chrono::system_clock::now(), prompt};
                std::thread{[&, m]() {
                    auto res = cli.Post("/chat", m.to_json_string(), "application/json");
                    if (!res || res.value().status != 200) {
                        std::scoped_lock<std::mutex> lock{messages_mutex};
                        messages.push_back({{"System"},
                                            std::chrono::system_clock::now(),
                                            "Error: There was an error sending your message!"});
                    }
                }}.detach();
                prompt = "";
            }
            return true;
        }
        if (ev == Event::Backspace) {
            if (!prompt.empty()) {
                prompt.pop_back();
            }
            // lazy utf8 workaround
            while (!prompt.empty() && (prompt.back() > 'z' || prompt.back() < 'A')) {
                prompt.pop_back();
            }
            return true;
        }
        if (ev.input() != "") {
            prompt += ev.input();
            return true;
        }
        return false;
    });

    auto screen = ScreenInteractive::TerminalOutput();
    std::thread{[&]() {
        while (true) {
            try {
                auto res = cli.Get(std::string{"/chat?last_update_time="} +
                                   time_to_string(last_update_time - std::chrono::seconds(1)));
                last_update_time = std::chrono::system_clock::now();
                if (!res || res.value().status != 200) {
                    throw 1;
                }
                std::vector<Message> new_messages;
                auto j = nlohmann::json::parse(res.value().body);
                for (auto e : j) {
                    new_messages.push_back(Message::from_json(e));
                }
                {
                    std::scoped_lock<std::mutex> lock{messages_mutex};
                    messages.insert(messages.end(), new_messages.begin(), new_messages.end());
                    std::sort(messages.begin(), messages.end());
                    auto r = std::unique(messages.begin(), messages.end());
                    messages.erase(r, messages.end());
                }
            } catch (...) {
                std::scoped_lock<std::mutex> lock{messages_mutex};
                messages.push_back({{"System"},
                                    std::chrono::system_clock::now(),
                                    "Error: There was an error recieving messages!"});
            }
            screen.PostEvent(Event::Custom);
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }}.detach();
    screen.Loop(main_comp);
}
