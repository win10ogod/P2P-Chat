#pragma once

/**
 * @file stun_client.h
 * @brief RFC 5389 STUN client for public endpoint discovery and NAT type detection.
 *
 * Implements the minimal subset of STUN (Session Traversal Utilities for NAT)
 * required to discover the client's public IP:Port mapping and classify the
 * NAT type (Full Cone, Restricted Cone, Port Restricted, Symmetric).
 */

#include "core/types.h"
#include "network/udp_socket.h"

namespace p2p {

/// NAT classification based on STUN probing results.
enum class NatType : uint8_t {
    Unknown         = 0,
    OpenInternet    = 1,  ///< No NAT, public IP directly reachable
    FullCone        = 2,  ///< Any external host can send to mapped port
    RestrictedCone  = 3,  ///< Only hosts we've sent to can reply (any port)
    PortRestricted  = 4,  ///< Only hosts we've sent to can reply (same port)
    Symmetric       = 5   ///< Different mapping per destination (hardest to traverse)
};

[[nodiscard]] inline std::string_view nat_type_str(NatType t) noexcept {
    switch (t) {
        case NatType::Unknown:        return "Unknown";
        case NatType::OpenInternet:   return "Open Internet";
        case NatType::FullCone:       return "Full Cone NAT";
        case NatType::RestrictedCone: return "Restricted Cone NAT";
        case NatType::PortRestricted: return "Port Restricted Cone NAT";
        case NatType::Symmetric:      return "Symmetric NAT";
    }
    return "Unknown";
}

/// Result of a STUN binding request.
struct StunResult {
    bool     success{false};
    Endpoint mapped_ep;       ///< Public IP:Port as seen by the STUN server
    NatType  nat_type{NatType::Unknown};
};

/**
 * @class StunClient
 * @brief Performs STUN Binding Requests to discover public endpoint and NAT type.
 *
 * Usage:
 *   StunClient stun;
 *   auto result = stun.query(socket, "stun.l.google.com", 19302);
 *   if (result.success) { ... use result.mapped_ep ... }
 */
class StunClient {
public:
    /// Default STUN servers (Google public STUN)
    static constexpr const char* kDefaultServer1 = "stun.l.google.com";
    static constexpr uint16_t    kDefaultPort1   = 19302;
    static constexpr const char* kDefaultServer2 = "stun1.l.google.com";
    static constexpr uint16_t    kDefaultPort2   = 19302;

    /// Perform a single STUN Binding Request and return the mapped endpoint.
    [[nodiscard]] StunResult query(UdpSocket& socket,
                                   const std::string& server = kDefaultServer1,
                                   uint16_t port = kDefaultPort1,
                                   int timeout_ms = 3000);

    /// Perform full NAT type detection using two STUN servers.
    [[nodiscard]] StunResult detect_nat_type(UdpSocket& socket,
                                             const std::string& server1 = kDefaultServer1,
                                             uint16_t port1 = kDefaultPort1,
                                             const std::string& server2 = kDefaultServer2,
                                             uint16_t port2 = kDefaultPort2,
                                             int timeout_ms = 3000);

private:
    /// Build a STUN Binding Request message (RFC 5389).
    [[nodiscard]] std::vector<uint8_t> build_binding_request() const;

    /// Parse a STUN Binding Response and extract the XOR-MAPPED-ADDRESS.
    [[nodiscard]] std::optional<Endpoint> parse_binding_response(
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& transaction_id) const;

    /// Resolve hostname to IP address.
    [[nodiscard]] std::string resolve_host(const std::string& hostname) const;
};

} // namespace p2p
