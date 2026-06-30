/**
 * @file server_main.cpp
 * @brief Lightweight signaling server for P2P Chat peer discovery.
 *
 * This server only facilitates initial address exchange and NAT traversal
 * coordination. No chat data passes through it.
 */

#include "core/types.h"
#include "network/udp_socket.h"
#include "protocol/protocol.h"
#include <csignal>
#include <iostream>
#include <map>
#include <mutex>

using namespace p2p;

static std::atomic<bool> g_running{true};

static void signal_handler(int /*sig*/) { g_running = false; }

/// Represents a registered peer on the signaling server.
struct PeerRecord {
    std::string name;
    std::string uuid;
    Endpoint    pub_ep;
    Endpoint    local_ep;
    uint16_t    port{0};
    uint64_t    last_seen{0};
};

/**
 * @class SignalingServer
 * @brief Handles peer registration, connection brokering, and stale peer cleanup.
 */
class SignalingServer {
public:
    bool start(uint16_t port) {
        auto ep = socket_.bind(port);
        if (!ep) {
            std::cerr << "Error: Failed to bind to port " << port << "\n";
            return false;
        }

        std::cout << "P2P Chat Signaling Server v2.0\n"
                  << "Listening on port " << ep->port << "\n"
                  << "Press Ctrl+C to stop.\n\n";

        socket_.start_receive([this](Datagram dg) { handle(std::move(dg)); });

        while (g_running) {
            std::this_thread::sleep_for(Millis(5000));
            cleanup_stale_peers();
        }

        socket_.stop_receive();
        socket_.close();
        std::cout << "\nServer stopped.\n";
        return true;
    }

private:
    void handle(Datagram dg) {
        auto pkt = Packet::deserialize(dg.data);
        if (!pkt) return;

        switch (pkt->header.type) {
        case PacketType::Register:
            handle_register(*pkt, dg.sender);
            break;
        case PacketType::ConnectRequest:
            handle_connect(*pkt);
            break;
        case PacketType::PeerList:
            handle_peer_list_request(dg.sender);
            break;
        case PacketType::Heartbeat:
            handle_heartbeat(dg.sender);
            break;
        case PacketType::Disconnect:
            handle_disconnect(dg.sender);
            break;
        default:
            break;
        }
    }

    void handle_register(const Packet& pkt, const Endpoint& sender) {
        auto d = PacketParser::parse_register(pkt);
        if (!d) return;

        std::lock_guard<std::mutex> lock(mtx_);
        peers_[d->uuid] = {d->name, d->uuid, sender, d->local_ep, d->port, now_ms()};
        std::cout << "[+] Registered: " << d->name << " (" << sender.to_string() << ")\n";

        auto ack = PacketFactory::make_register_ack(true, "OK", sender);
        (void)socket_.send_to(ack.serialize(), sender);
        broadcast_peer_list();
    }

    void handle_connect(const Packet& pkt) {
        ByteReader r(pkt.payload);
        auto from_uuid = r.read_string();
        auto to_uuid = r.read_string();
        if (!r.is_valid()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        auto fi = peers_.find(from_uuid);
        auto ti = peers_.find(to_uuid);
        if (fi == peers_.end() || ti == peers_.end()) return;

        const auto& f = fi->second;
        const auto& t = ti->second;
        std::cout << "[~] Connect: " << f.name << " -> " << t.name << "\n";

        // Notify both peers of each other's endpoints
        auto n1 = PacketFactory::make_punch_notify(t.uuid, t.name, t.pub_ep, t.local_ep);
        (void)socket_.send_to(n1.serialize(), f.pub_ep);

        auto n2 = PacketFactory::make_punch_notify(f.uuid, f.name, f.pub_ep, f.local_ep);
        (void)socket_.send_to(n2.serialize(), t.pub_ep);
    }

    void handle_peer_list_request(const Endpoint& sender) {
        std::lock_guard<std::mutex> lock(mtx_);
        send_peer_list_to(sender);
    }

    void handle_heartbeat(const Endpoint& sender) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto& [_, p] : peers_) {
            if (p.pub_ep == sender) {
                p.last_seen = now_ms();
                break;
            }
        }
    }

    void handle_disconnect(const Endpoint& sender) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (auto it = peers_.begin(); it != peers_.end(); ++it) {
            if (it->second.pub_ep == sender) {
                std::cout << "[-] Disconnected: " << it->second.name << "\n";
                peers_.erase(it);
                break;
            }
        }
        broadcast_peer_list();
    }

    void broadcast_peer_list() {
        for (const auto& [_, p] : peers_) {
            send_peer_list_to(p.pub_ep);
        }
    }

    void send_peer_list_to(const Endpoint& target) {
        std::vector<PacketFactory::PeerEntry> entries;
        entries.reserve(peers_.size());
        for (const auto& [_, p] : peers_) {
            entries.push_back({p.name, p.uuid, p.pub_ep, p.local_ep});
        }
        auto pkt = PacketFactory::make_peer_list(entries);
        (void)socket_.send_to(pkt.serialize(), target);
    }

    void cleanup_stale_peers() {
        std::lock_guard<std::mutex> lock(mtx_);
        bool changed = false;
        for (auto it = peers_.begin(); it != peers_.end();) {
            if (now_ms() - it->second.last_seen > config::kPeerStaleTimeoutMs) {
                std::cout << "[x] Stale: " << it->second.name << "\n";
                it = peers_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
        if (changed) broadcast_peer_list();
    }

    UdpSocket                          socket_;
    std::map<std::string, PeerRecord>  peers_;
    std::mutex                         mtx_;
};

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    uint16_t port = config::kDefaultSignalingPort;
    if (argc > 1) {
        port = static_cast<uint16_t>(std::stoi(argv[1]));
    }

    SignalingServer server;
    server.start(port);
    return 0;
}
