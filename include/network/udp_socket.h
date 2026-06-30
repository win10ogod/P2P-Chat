#pragma once

/**
 * @file udp_socket.h
 * @brief RAII UDP socket abstraction with cross-platform support.
 *
 * Provides a non-blocking, thread-safe UDP socket that works on
 * Windows (Winsock2), macOS, and Linux (POSIX sockets). The socket
 * is automatically closed on destruction.
 */

#include "core/types.h"
#include <thread>
#include <functional>

namespace p2p {

/// Represents a received UDP datagram with sender information.
struct Datagram {
    std::vector<uint8_t> data;
    Endpoint             sender;
};

/**
 * @class UdpSocket
 * @brief Cross-platform RAII UDP socket with async receive support.
 */
class UdpSocket {
public:
    using ReceiveHandler = std::function<void(Datagram)>;

    UdpSocket();
    ~UdpSocket();

    // Non-copyable, movable
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    /// Bind to a local port (0 = auto-assign). Returns the bound endpoint.
    [[nodiscard]] std::optional<Endpoint> bind(uint16_t port = 0);

    /// Start async receive loop in a background thread.
    void start_receive(ReceiveHandler handler);

    /// Stop the receive loop.
    void stop_receive();

    /// Send raw data to a target endpoint. Returns bytes sent or -1 on error.
    [[nodiscard]] int send_to(const std::vector<uint8_t>& data, const Endpoint& target);

    /// Send raw data to a target endpoint (pointer + size overload).
    [[nodiscard]] int send_to(const uint8_t* data, size_t len, const Endpoint& target);

    /// Get the local port this socket is bound to.
    [[nodiscard]] uint16_t local_port() const noexcept { return local_port_; }

    /// Check if the socket is valid and bound.
    [[nodiscard]] bool is_open() const noexcept;

    /// Close the socket explicitly.
    void close();

    /// Get local machine's primary IP address.
    [[nodiscard]] static std::string get_local_ip();

private:
    void receive_loop();

#ifdef _WIN32
    using SocketHandle = uintptr_t;
    static constexpr SocketHandle kInvalidSocket = ~static_cast<uintptr_t>(0);
#else
    using SocketHandle = int;
    static constexpr SocketHandle kInvalidSocket = -1;
#endif

    SocketHandle      fd_{kInvalidSocket};
    uint16_t          local_port_{0};
    std::atomic<bool> receiving_{false};
    std::thread       recv_thread_;
    ReceiveHandler    handler_;
};

} // namespace p2p
