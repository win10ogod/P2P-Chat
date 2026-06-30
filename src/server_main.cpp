/**
 * @file server_main.cpp
 * @brief Lightweight signaling server with relay support for P2P Chat.
 *
 * This server facilitates:
 *   1. Peer registration and discovery.
 *   2. NAT traversal coordination (hole-punch notification).
 *   3. Relay forwarding when direct P2P connection fails.
 */

#include "core/types.h"
#include "network/udp_socket.h"
#include "protocol/protocol.h"
#include <csignal>
#include <iostream>
#include <map>
#include <mutex>
#include <set>

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
    bool        relay_allocated{false};
};

/**
 * @class SignalingServer
 * @brief Handles peer registration, connection brokering, relay, and stale peer cleanup.
 */
class SignalingServer {
public:
    bool start(uint16_t port) {
        auto ep = socket_.bind(port);
        if (!ep) {
            std::cerr << "Error: Failed to bind to port " << port << "\n";
            return false;
        }

        std::cout << "P2P Chat Signaling Server v2.1\n"
                  << "Listening on port " << ep->port << "\n"
                  << "Relay support: enabled\n"
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
        case PacketType::RelayAllocate:
            handle_relay_allocate(*pkt, dg.sender);
            break;
        case PacketType::RelayData:
            handle_relay_data(*pkt, dg.sender);
            break;
        case PacketType::RelayRelease:
            handle_relay_release(*pkt, dg.sender);
            break;
        default:
            break;
        }
    }

    void handle_register(const Packet& pkt, const Endpoint& sender) {
        auto d = PacketParser::parse_register(pkt);
        if (!d) return;

        std::lock_guard<std::mutex> lock(mtx_);
        peers_[d->uuid] = {d->name, d->uuid, sender, d->local_ep, d->port, now_ms(), false};
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
                relay_peers_.erase(it->second.uuid);
                peers_.erase(it);
                break;
            }
        }
        broadcast_peer_list();
    }

    // ─── Relay Handling ──────────────────────────────────────────────────────

    void handle_relay_allocate(const Packet& pkt, const Endpoint& sender) {
        ByteReader r(pkt.payload);
        auto uuid = r.read_string();
        if (!r.is_valid()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        auto it = peers_.find(uuid);
        if (it == peers_.end()) return;

        it->second.relay_allocated = true;
        relay_peers_.insert(uuid);
        std::cout << "[R] Relay allocated for: " << it->second.name << "\n";

        // Respond with the server's own endpoint as the relay address
        Packet resp;
        resp.header.type = PacketType::RelayAllocateOk;
        ByteWriter w;
        w.write_string(sender.ip); // The relay address is the server itself
        w.write_u16(socket_.local_port());
        resp.payload = w.take();
        resp.header.payload_len = static_cast<uint16_t>(resp.payload.size());
        (void)socket_.send_to(resp.serialize(), sender);
    }

    void handle_relay_data(const Packet& pkt, const Endpoint& /*sender*/) {
        ByteReader r(pkt.payload);
        auto from_uuid = r.read_string();
        auto to_uuid = r.read_string();
        auto payload = r.read_bytes();
        if (!r.is_valid()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        auto target_it = peers_.find(to_uuid);
        if (target_it == peers_.end()) return;

        // Forward the data to the target peer, preserving from/to metadata
        Packet fwd;
        fwd.header.type = PacketType::RelayData;
        ByteWriter w;
        w.write_string(from_uuid);
        w.write_string(to_uuid);
        w.write_bytes(payload);
        fwd.payload = w.take();
        fwd.header.payload_len = static_cast<uint16_t>(fwd.payload.size());

        (void)socket_.send_to(fwd.serialize(), target_it->second.pub_ep);
    }

    void handle_relay_release(const Packet& pkt, const Endpoint& /*sender*/) {
        ByteReader r(pkt.payload);
        auto uuid = r.read_string();
        if (!r.is_valid()) return;

        std::lock_guard<std::mutex> lock(mtx_);
        auto it = peers_.find(uuid);
        if (it != peers_.end()) {
            it->second.relay_allocated = false;
            relay_peers_.erase(uuid);
            std::cout << "[R] Relay released for: " << it->second.name << "\n";
        }
    }

    // ─── Utilities ───────────────────────────────────────────────────────────

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
                relay_peers_.erase(it->second.uuid);
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
    std::set<std::string>              relay_peers_;
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
