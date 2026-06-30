#include "core/session.h"

namespace p2p {

Session::Session() = default;
Session::~Session() { shutdown(); }

bool Session::init(const std::string& username, uint16_t port) {
    if (initialized_) return true;

    local_id_.name = username;
    local_id_.uuid = generate_uuid();

    auto ep = socket_.bind(port);
    if (!ep) return false;
    local_ep_ = *ep;

    signaling_ = std::make_unique<SignalingClient>(socket_);

    signaling_->on_registered([this](bool ok, const Endpoint& pub) {
        if (ok) {
            public_ep_ = pub;
            events_.push({ChatEvent::ServerConnected, "", "", "Connected to server"});
        } else {
            events_.push({ChatEvent::Error, "", "", "Server registration failed"});
        }
    });

    signaling_->on_peer_list([this](const std::vector<PacketFactory::PeerEntry>& peers) {
        {
            std::unique_lock lock(peers_mtx_);
            peer_list_ = peers;
        }
        events_.push({ChatEvent::PeerListUpdated, "", "", ""});
    });

    signaling_->on_punch_notify([this](const std::string& uuid, const std::string& name,
                                       const Endpoint& pub, const Endpoint& local) {
        on_punch_notify(uuid, name, pub, local);
    });

    socket_.start_receive([this](Datagram dg) { on_datagram(std::move(dg)); });
    initialized_ = true;
    return true;
}

void Session::shutdown() {
    if (!initialized_) return;
    {
        std::unique_lock lock(conns_mtx_);
        for (auto& [_, c] : conns_) c->disconnect();
        conns_.clear();
    }
    if (signaling_) signaling_->stop_heartbeat();
    socket_.stop_receive();
    socket_.close();
    signaling_.reset();
    initialized_ = false;
}

bool Session::connect_server(const std::string& ip, uint16_t port) {
    if (!initialized_ || !signaling_) return false;
    signaling_->register_self({ip, port}, local_id_.name, local_id_.uuid,
                              local_ep_, socket_.local_port());
    return true;
}

void Session::connect_peer(const std::string& uuid) {
    if (!signaling_ || !signaling_->is_registered()) return;
    signaling_->request_connect(uuid);
}

void Session::disconnect_peer(const std::string& uuid) {
    std::unique_lock lock(conns_mtx_);
    auto it = conns_.find(uuid);
    if (it != conns_.end()) {
        it->second->disconnect();
        conns_.erase(it);
    }
}

bool Session::send_text(const std::string& uuid, const std::string& text) {
    std::shared_lock lock(conns_mtx_);
    auto it = conns_.find(uuid);
    if (it == conns_.end()) return false;
    return it->second->send_text(text, local_id_.uuid);
}

bool Session::broadcast_text(const std::string& text) {
    std::shared_lock lock(conns_mtx_);
    bool any = false;
    for (auto& [_, c] : conns_) {
        if (c->state() == ConnState::Connected) {
            c->send_text(text, local_id_.uuid);
            any = true;
        }
    }
    return any;
}

bool Session::send_audio(const std::string& uuid, uint32_t seq, uint64_t ts,
                         const std::vector<uint8_t>& opus) {
    std::shared_lock lock(conns_mtx_);
    auto it = conns_.find(uuid);
    if (it == conns_.end()) return false;
    return it->second->send_audio(seq, ts, opus);
}

void Session::refresh_peers() {
    if (signaling_ && signaling_->is_registered()) {
        signaling_->request_peer_list();
    }
}

std::optional<ChatEvent> Session::poll_event() {
    return events_.try_pop();
}

std::vector<PacketFactory::PeerEntry> Session::known_peers() const {
    std::shared_lock lock(peers_mtx_);
    return peer_list_;
}

bool Session::is_connected(const std::string& uuid) const {
    std::shared_lock lock(conns_mtx_);
    auto it = conns_.find(uuid);
    return it != conns_.end() && it->second->state() == ConnState::Connected;
}

void Session::on_datagram(Datagram dg) {
    auto pkt = Packet::deserialize(dg.data);
    if (!pkt) return;

    switch (pkt->header.type) {
    case PacketType::RegisterAck:
    case PacketType::PeerList:
    case PacketType::PunchNotify:
        if (signaling_) signaling_->handle_packet(*pkt, dg.sender);
        break;
    default: {
        std::shared_lock lock(conns_mtx_);
        auto* conn = find_by_endpoint(dg.sender);
        if (conn) {
            conn->handle_packet(*pkt, dg.sender);
        } else {
            // Route to any connection in Punching state (NAT traversal in progress)
            for (auto& [_, c] : conns_) {
                if (c->state() == ConnState::Punching) {
                    c->handle_packet(*pkt, dg.sender);
                    break; // Only deliver to the first punching connection
                }
            }
        }
        break;
    }
    }
}

void Session::on_punch_notify(const std::string& uuid, const std::string& name,
                              const Endpoint& pub, const Endpoint& local) {
    std::unique_lock lock(conns_mtx_);
    if (conns_.count(uuid)) return; // Already connecting/connected

    PeerId peer{name, uuid};
    auto conn = std::make_unique<P2PConnection>(socket_, peer);

    conn->on_text([this, uuid, name](const std::string& text, uint32_t /*msg_id*/) {
        events_.push({ChatEvent::TextReceived, uuid, name, text});
    });

    conn->on_state([this, uuid, name](ConnState s) {
        if (s == ConnState::Connected)
            events_.push({ChatEvent::PeerConnected, uuid, name, ""});
        else if (s == ConnState::Disconnected || s == ConnState::Failed)
            events_.push({ChatEvent::PeerDisconnected, uuid, name, ""});
    });

    conn->start_punch(pub, local);
    conns_[uuid] = std::move(conn);
}

P2PConnection* Session::find_by_endpoint(const Endpoint& ep) {
    for (auto& [_, c] : conns_) {
        if (c->remote_endpoint() == ep) return c.get();
    }
    return nullptr;
}

} // namespace p2p
