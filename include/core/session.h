#pragma once

/**
 * @file session.h
 * @brief Central session manager coordinating P2P connections, STUN, TURN, and signaling.
 */

#include "core/types.h"
#include "network/udp_socket.h"
#include "network/p2p_connection.h"
#include "network/signaling.h"
#include "network/stun_client.h"
#include "network/turn_client.h"
#include <map>
#include <memory>

namespace p2p {

/// Events emitted by Session for consumption by the UI layer.
struct ChatEvent {
    enum Type {
        ServerConnected,
        ServerDisconnected,
        PeerConnected,
        PeerDisconnected,
        PeerListUpdated,
        TextReceived,
        AudioReceived,
        NatDetected,
        RelayActivated,
        Error
    };

    Type        type;
    std::string peer_uuid;
    std::string peer_name;
    std::string text;
};

/**
 * @class Session
 * @brief Owns the UDP socket, signaling client, STUN/TURN clients, and all P2P connections.
 *
 * Session is the central coordinator: it dispatches incoming datagrams to
 * either the signaling client, TURN client, or the appropriate P2P connection,
 * and exposes a poll-based event queue for the GUI to consume.
 */
class Session {
public:
    Session();
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /// Initialize with username. Returns false on bind failure.
    [[nodiscard]] bool init(const std::string& username, uint16_t port = 0);

    /// Gracefully shut down all connections and the socket.
    void shutdown();

    /// Connect to the signaling server.
    bool connect_server(const std::string& ip, uint16_t port);

    /// Request P2P connection to a peer (via signaling).
    void connect_peer(const std::string& uuid);

    /// Disconnect from a specific peer.
    void disconnect_peer(const std::string& uuid);

    /// Send a text message to a specific peer.
    bool send_text(const std::string& uuid, const std::string& text);

    /// Broadcast text to all connected peers.
    bool broadcast_text(const std::string& text);

    /// Send an audio frame to a specific peer.
    bool send_audio(const std::string& uuid, uint32_t seq, uint64_t ts,
                    const std::vector<uint8_t>& opus);

    /// Request a peer list refresh from the signaling server.
    void refresh_peers();

    /// Perform STUN-based NAT type detection.
    void detect_nat();

    /// Activate relay fallback for a peer when direct P2P fails.
    bool activate_relay(const std::string& peer_uuid);

    /// Poll the next event (non-blocking).
    [[nodiscard]] std::optional<ChatEvent> poll_event();

    // ─── Accessors ───────────────────────────────────────────────────────────
    [[nodiscard]] const PeerId&   local_id()   const noexcept { return local_id_; }
    [[nodiscard]] const Endpoint& local_ep()   const noexcept { return local_ep_; }
    [[nodiscard]] const Endpoint& public_ep()  const noexcept { return public_ep_; }
    [[nodiscard]] NatType         nat_type()   const noexcept { return nat_type_; }
    [[nodiscard]] bool            is_initialized() const noexcept { return initialized_; }

    [[nodiscard]] std::vector<PacketFactory::PeerEntry> known_peers() const;
    [[nodiscard]] bool is_connected(const std::string& uuid) const;

private:
    void on_datagram(Datagram dg);
    void on_punch_notify(const std::string& uuid, const std::string& name,
                         const Endpoint& pub, const Endpoint& local);
    P2PConnection* find_by_endpoint(const Endpoint& ep);

    UdpSocket                                             socket_;
    std::unique_ptr<SignalingClient>                       signaling_;
    StunClient                                            stun_;
    std::unique_ptr<TurnClient>                           turn_;
    PeerId                                                local_id_;
    Endpoint                                              local_ep_;
    Endpoint                                              public_ep_;
    NatType                                               nat_type_{NatType::Unknown};
    std::map<std::string, std::unique_ptr<P2PConnection>> conns_;
    std::vector<PacketFactory::PeerEntry>                 peer_list_;
    ConcurrentQueue<ChatEvent>                            events_;
    mutable std::shared_mutex                             conns_mtx_;
    mutable std::shared_mutex                             peers_mtx_;
    bool                                                  initialized_{false};
};

} // namespace p2p
