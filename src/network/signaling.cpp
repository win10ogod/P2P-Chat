#include "network/signaling.h"

namespace p2p {

SignalingClient::SignalingClient(UdpSocket& socket) : socket_(socket) {}

SignalingClient::~SignalingClient() { stop_heartbeat(); }

void SignalingClient::register_self(const Endpoint& server, const std::string& name,
                                    const std::string& uuid, const Endpoint& local_ep,
                                    uint16_t port) {
    server_ep_ = server;
    my_uuid_ = uuid;
    auto pkt = PacketFactory::make_register(name, uuid, local_ep, port);
    (void)socket_.send_to(pkt.serialize(), server_ep_);
}

void SignalingClient::request_connect(const std::string& peer_uuid) {
    if (!registered_) return;
    auto pkt = PacketFactory::make_connect_request(my_uuid_, peer_uuid);
    (void)socket_.send_to(pkt.serialize(), server_ep_);
}

void SignalingClient::request_peer_list() {
    if (!registered_) return;
    // Send empty PeerList packet as a request signal
    Packet pkt;
    pkt.header.type = PacketType::PeerList;
    pkt.header.payload_len = 0;
    (void)socket_.send_to(pkt.serialize(), server_ep_);
}

void SignalingClient::handle_packet(const Packet& pkt, const Endpoint& /*from*/) {
    switch (pkt.header.type) {
    case PacketType::RegisterAck: {
        auto d = PacketParser::parse_register_ack(pkt);
        if (d) {
            registered_ = d->ok;
            if (reg_cb_) reg_cb_(d->ok, d->pub_ep);
            if (d->ok) start_heartbeat();
        }
        break;
    }
    case PacketType::PeerList: {
        auto d = PacketParser::parse_peer_list(pkt);
        if (d && list_cb_) list_cb_(d->peers);
        break;
    }
    case PacketType::PunchNotify: {
        auto d = PacketParser::parse_punch_notify(pkt);
        if (d && punch_cb_) punch_cb_(d->uuid, d->name, d->pub_ep, d->local_ep);
        break;
    }
    default:
        break;
    }
}

void SignalingClient::start_heartbeat() {
    if (hb_running_) return;
    hb_running_ = true;
    hb_thread_ = std::thread(&SignalingClient::heartbeat_loop, this);
}

void SignalingClient::stop_heartbeat() {
    hb_running_ = false;
    if (hb_thread_.joinable()) hb_thread_.join();
}

void SignalingClient::heartbeat_loop() {
    while (hb_running_) {
        std::this_thread::sleep_for(Millis(config::kHeartbeatIntervalMs));
        if (!hb_running_) break;
        auto pkt = PacketFactory::heartbeat();
        (void)socket_.send_to(pkt.serialize(), server_ep_);
    }
}

} // namespace p2p
