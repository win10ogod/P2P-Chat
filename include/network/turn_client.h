#pragma once

/**
 * @file turn_client.h
 * @brief TURN (Traversal Using Relays around NAT) client for relay fallback.
 *
 * When direct P2P hole-punching fails (e.g., both peers are behind Symmetric NAT),
 * the TURN client allocates a relay address on a TURN server and routes traffic
 * through it. This guarantees connectivity at the cost of increased latency.
 *
 * This implementation provides a simplified TURN-like relay protocol that works
 * with our own signaling server acting as the relay, avoiding the complexity of
 * full RFC 5766 while providing the same functional benefit.
 */

#include "core/types.h"
#include "network/udp_socket.h"
#include "protocol/protocol.h"

namespace p2p {

/// State of the relay connection.
enum class RelayState : uint8_t {
    Idle = 0,
    Allocating,
    Active,
    Failed
};

/**
 * @class TurnClient
 * @brief Manages a relay allocation for traffic forwarding when P2P fails.
 *
 * The relay protocol works as follows:
 *   1. Client sends RelayAllocate to the signaling server.
 *   2. Server responds with a RelayAllocateOk containing the relay address.
 *   3. Client sends data prefixed with the target peer's UUID.
 *   4. Server strips the prefix and forwards to the target peer's relay channel.
 *
 * This is a lightweight alternative to full RFC 5766 TURN, designed to work
 * with our existing signaling infrastructure.
 */
class TurnClient {
public:
    using DataCallback = std::function<void(const std::string& from_uuid,
                                            const std::vector<uint8_t>& data)>;

    explicit TurnClient(UdpSocket& socket);
    ~TurnClient();

    TurnClient(const TurnClient&) = delete;
    TurnClient& operator=(const TurnClient&) = delete;

    /// Request a relay allocation from the server.
    bool allocate(const Endpoint& relay_server, const std::string& my_uuid);

    /// Send data through the relay to a specific peer.
    bool send_relayed(const std::string& target_uuid, const std::vector<uint8_t>& data);

    /// Handle an incoming relay packet from the server.
    void handle_packet(const Packet& pkt, const Endpoint& from);

    /// Release the relay allocation.
    void release();

    /// Register callback for relayed data received from peers.
    void on_data(DataCallback cb) { data_cb_ = std::move(cb); }

    [[nodiscard]] RelayState state() const noexcept { return state_.load(); }
    [[nodiscard]] const Endpoint& relay_endpoint() const noexcept { return relay_ep_; }
    [[nodiscard]] bool is_active() const noexcept { return state_ == RelayState::Active; }

private:
    UdpSocket&          socket_;
    Endpoint            server_ep_;
    Endpoint            relay_ep_;
    std::string         my_uuid_;
    std::atomic<RelayState> state_{RelayState::Idle};
    DataCallback        data_cb_;
};

} // namespace p2p
