#pragma once
/**
 * @file signaling.h
 * @brief Signaling client for server registration and peer discovery.
 */
#include "core/types.h"
#include "network/udp_socket.h"
#include "protocol/protocol.h"
#include <functional>
#include <thread>

namespace p2p {

class SignalingClient {
public:
    using PeerListCallback  = std::function<void(const std::vector<PacketFactory::PeerEntry>&)>;
    using PunchCallback     = std::function<void(const std::string& uuid, const std::string& name,
                                                  const Endpoint& pub, const Endpoint& local)>;
    using RegisterCallback  = std::function<void(bool ok, const Endpoint& pub_ep)>;

    explicit SignalingClient(UdpSocket& socket);
    ~SignalingClient();

    /// Register with the signaling server.
    void register_self(const Endpoint& server, const std::string& name,
                       const std::string& uuid, const Endpoint& local_ep, uint16_t port);

    /// Request connection to a peer.
    void request_connect(const std::string& peer_uuid);

    /// Request peer list refresh.
    void request_peer_list();

    /// Handle incoming signaling packet.
    void handle_packet(const Packet& pkt, const Endpoint& from);

    /// Start heartbeat to server.
    void start_heartbeat();
    void stop_heartbeat();

    void on_registered(RegisterCallback cb)  { reg_cb_ = std::move(cb); }
    void on_peer_list(PeerListCallback cb)   { list_cb_ = std::move(cb); }
    void on_punch_notify(PunchCallback cb)   { punch_cb_ = std::move(cb); }

    [[nodiscard]] bool is_registered() const noexcept { return registered_; }
    [[nodiscard]] const Endpoint& server_ep() const noexcept { return server_ep_; }

private:
    void heartbeat_loop();

    UdpSocket&    socket_;
    Endpoint      server_ep_;
    std::string   my_uuid_;
    bool          registered_{false};
    std::atomic<bool> hb_running_{false};
    std::thread   hb_thread_;

    RegisterCallback reg_cb_;
    PeerListCallback list_cb_;
    PunchCallback    punch_cb_;
};

} // namespace p2p
