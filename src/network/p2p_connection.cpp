#include "network/p2p_connection.h"

namespace p2p {

P2PConnection::P2PConnection(UdpSocket& socket, PeerId remote)
    : socket_(socket), remote_(std::move(remote)) {}

P2PConnection::~P2PConnection() {
    disconnect();
}

void P2PConnection::set_state(ConnState s) {
    ConnState expected = state_.load();
    if (expected == s) return;
    state_.store(s);
    if (state_cb_) state_cb_(s);
}

void P2PConnection::start_punch(const Endpoint& pub_ep, const Endpoint& local_ep) {
    pub_ep_ = pub_ep;
    local_ep_ = local_ep;
    set_state(ConnState::Punching);
    punching_ = true;
    last_recv_ = Clock::now(); // Initialize to avoid premature timeout
    punch_thread_ = std::thread(&P2PConnection::punch_loop, this);
}

void P2PConnection::punch_loop() {
    uint32_t attempts = 0;
    while (punching_ && attempts < config::kPunchMaxAttempts) {
        auto probe = PacketFactory::make_punch_probe(remote_.uuid);
        send_packet(probe);

        // Also try local endpoint for LAN connectivity
        if (local_ep_.is_valid()) {
            auto data = probe.serialize();
            (void)socket_.send_to(data, local_ep_);
        }

        std::this_thread::sleep_for(Millis(config::kPunchIntervalMs));
        ++attempts;
    }

    if (punching_) {
        // Exhausted all attempts without success
        set_state(ConnState::Failed);
    }
}

void P2PConnection::start_heartbeat() {
    std::lock_guard<std::mutex> lock(heartbeat_start_mtx_);
    if (alive_) return; // Already running
    alive_ = true;
    last_recv_ = Clock::now();
    heartbeat_thread_ = std::thread(&P2PConnection::heartbeat_loop, this);
}

void P2PConnection::heartbeat_loop() {
    while (alive_) {
        std::this_thread::sleep_for(Millis(config::kHeartbeatIntervalMs));
        if (!alive_) break;

        auto hb = PacketFactory::heartbeat();
        send_packet(hb);

        // Check for connection timeout
        auto elapsed = std::chrono::duration_cast<Millis>(Clock::now() - last_recv_).count();
        if (elapsed > static_cast<int64_t>(config::kConnectionTimeoutMs)) {
            alive_ = false;
            set_state(ConnState::Disconnected);
        }
    }
}

void P2PConnection::handle_packet(const Packet& pkt, const Endpoint& from) {
    last_recv_ = Clock::now();

    switch (pkt.header.type) {
    case PacketType::PunchProbe: {
        confirmed_ep_ = from;
        auto ack = PacketFactory::make_punch_ack(remote_.uuid);
        auto data = ack.serialize();
        (void)socket_.send_to(data, from);

        if (state_.load() == ConnState::Punching) {
            punching_ = false;
            set_state(ConnState::Connected);
            start_heartbeat();
        }
        break;
    }
    case PacketType::PunchAck: {
        if (state_.load() == ConnState::Punching) {
            confirmed_ep_ = from;
            punching_ = false;
            set_state(ConnState::Connected);
            start_heartbeat();
        }
        break;
    }
    case PacketType::Heartbeat: {
        auto ack = PacketFactory::heartbeat_ack(pkt.header.seq_no);
        send_packet(ack);
        break;
    }
    case PacketType::HeartbeatAck:
        // Acknowledged; last_recv_ already updated above
        break;
    case PacketType::TextMessage: {
        auto td = PacketParser::parse_text(pkt);
        if (td && text_cb_) {
            text_cb_(td->text, td->id);
            auto ack = PacketFactory::make_text_ack(td->id);
            send_packet(ack);
        }
        break;
    }
    case PacketType::AudioFrame: {
        auto ad = PacketParser::parse_audio(pkt);
        if (ad && audio_cb_) audio_cb_(ad->seq, ad->ts, ad->opus);
        break;
    }
    case PacketType::Disconnect:
        alive_ = false;
        set_state(ConnState::Disconnected);
        break;
    default:
        break;
    }
}

bool P2PConnection::send_text(const std::string& text, const std::string& sender_uuid) {
    if (state_.load() != ConnState::Connected) return false;
    auto pkt = PacketFactory::make_text(sender_uuid, text, ++msg_counter_);
    send_packet(pkt);
    return true;
}

bool P2PConnection::send_audio(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus) {
    if (state_.load() != ConnState::Connected) return false;
    auto pkt = PacketFactory::make_audio(seq, ts, opus);
    send_packet(pkt);
    return true;
}

void P2PConnection::disconnect() {
    if (state_.load() == ConnState::Connected) {
        auto pkt = PacketFactory::make_disconnect();
        send_packet(pkt);
    }
    punching_ = false;
    alive_ = false;
    if (punch_thread_.joinable()) punch_thread_.join();
    if (heartbeat_thread_.joinable()) heartbeat_thread_.join();
    set_state(ConnState::Disconnected);
}

void P2PConnection::send_packet(const Packet& pkt) {
    std::lock_guard<std::mutex> lock(send_mtx_);
    auto data = pkt.serialize();
    if (confirmed_ep_.is_valid()) {
        (void)socket_.send_to(data, confirmed_ep_);
    } else if (pub_ep_.is_valid()) {
        (void)socket_.send_to(data, pub_ep_);
    }
}

} // namespace p2p
