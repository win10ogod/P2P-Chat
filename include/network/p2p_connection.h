#pragma once

/**
 * @file p2p_connection.h
 * @brief Manages a single P2P connection with hole-punching and keepalive.
 */

#include "core/types.h"
#include "network/udp_socket.h"
#include "protocol/protocol.h"
#include <functional>
#include <thread>

namespace p2p {

/**
 * @class P2PConnection
 * @brief Represents a single peer-to-peer connection.
 *
 * Handles the full lifecycle: NAT hole-punching, heartbeat keepalive,
 * text/audio data exchange, and graceful disconnection.
 */
class P2PConnection {
public:
    using TextCallback  = std::function<void(const std::string& text, uint32_t msg_id)>;
    using AudioCallback = std::function<void(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus)>;
    using StateCallback = std::function<void(ConnState)>;

    P2PConnection(UdpSocket& socket, PeerId remote);
    ~P2PConnection();

    // Non-copyable, non-movable
    P2PConnection(const P2PConnection&) = delete;
    P2PConnection& operator=(const P2PConnection&) = delete;

    /// Begin hole-punching towards the remote peer.
    void start_punch(const Endpoint& pub_ep, const Endpoint& local_ep);

    /// Handle an incoming packet from this peer.
    void handle_packet(const Packet& pkt, const Endpoint& from);

    /// Send a text message.
    bool send_text(const std::string& text, const std::string& sender_uuid);

    /// Send an audio frame.
    bool send_audio(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus);

    /// Disconnect gracefully.
    void disconnect();

    // Callback registration
    void on_text(TextCallback cb)   { text_cb_ = std::move(cb); }
    void on_audio(AudioCallback cb) { audio_cb_ = std::move(cb); }
    void on_state(StateCallback cb) { state_cb_ = std::move(cb); }

    [[nodiscard]] ConnState       state()           const noexcept { return state_.load(); }
    [[nodiscard]] const PeerId&   remote_peer()     const noexcept { return remote_; }
    [[nodiscard]] const Endpoint& remote_endpoint() const noexcept { return confirmed_ep_; }

private:
    void set_state(ConnState s);
    void punch_loop();
    void heartbeat_loop();
    void start_heartbeat();
    void send_packet(const Packet& pkt);

    UdpSocket&          socket_;
    PeerId              remote_;
    Endpoint            pub_ep_;
    Endpoint            local_ep_;
    Endpoint            confirmed_ep_;
    std::atomic<ConnState> state_{ConnState::Disconnected};

    std::atomic<bool>   punching_{false};
    std::atomic<bool>   alive_{false};
    std::thread         punch_thread_;
    std::thread         heartbeat_thread_;
    uint32_t            msg_counter_{0};
    TimePoint           last_recv_{Clock::now()};

    TextCallback        text_cb_;
    AudioCallback       audio_cb_;
    StateCallback       state_cb_;
    std::mutex          send_mtx_;
    std::mutex          heartbeat_start_mtx_;
};

} // namespace p2p
