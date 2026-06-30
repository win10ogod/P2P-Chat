/**
 * @file main.cpp
 * @brief P2P Chat application entry point.
 */

#include "core/session.h"
#include "ui/chat_window.h"
#include <cstring>
#include <iostream>
#include <string>

using namespace p2p;

struct Config {
    std::string username;
    std::string server_ip;
    uint16_t    server_port{config::kDefaultSignalingPort};
    uint16_t    listen_port{0};
};

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-u" || arg == "--username") && i + 1 < argc) {
            cfg.username = argv[++i];
        } else if ((arg == "-s" || arg == "--server") && i + 1 < argc) {
            std::string s = argv[++i];
            auto colon = s.rfind(':');
            if (colon != std::string::npos) {
                cfg.server_ip = s.substr(0, colon);
                cfg.server_port = static_cast<uint16_t>(std::stoi(s.substr(colon + 1)));
            } else {
                cfg.server_ip = s;
            }
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            cfg.listen_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: p2pchat [options]\n"
                      << "  -u, --username <name>    Set display name\n"
                      << "  -s, --server <ip:port>   Signaling server address\n"
                      << "  -p, --port <port>        Local listen port (0 = auto)\n"
                      << "  -h, --help               Show this help\n";
            std::exit(0);
        }
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    Config cfg = parse_args(argc, argv);

    Session session;
    ChatWindow window;

    if (!window.init(1100, 750)) {
        std::cerr << "Error: Failed to initialize GUI window.\n";
        return 1;
    }

    // Pre-initialize session if username was provided via CLI
    if (!cfg.username.empty()) {
        if (!session.init(cfg.username, cfg.listen_port)) {
            std::cerr << "Error: Failed to bind UDP socket.\n";
            return 1;
        }
        if (!cfg.server_ip.empty()) {
            session.connect_server(cfg.server_ip, cfg.server_port);
        }
    }

    window.run(session);
    window.shutdown();
    session.shutdown();
    return 0;
}
