#include "network/stun_client.h"

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <netdb.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

#include <cstring>
#include <random>

namespace p2p {

// ─── STUN Constants (RFC 5389) ───────────────────────────────────────────────

static constexpr uint16_t kStunBindingResponse = 0x0101;
static constexpr uint32_t kStunMagicCookie     = 0x2112A442;
static constexpr uint16_t kAttrMappedAddress   = 0x0001;
static constexpr uint16_t kAttrXorMappedAddr   = 0x0020;
static constexpr size_t   kStunHeaderSize      = 20;
static constexpr size_t   kTransactionIdSize   = 12;

// ─── Build Binding Request ───────────────────────────────────────────────────

std::vector<uint8_t> StunClient::build_binding_request() const {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 255);

    std::vector<uint8_t> msg(kStunHeaderSize);
    // Message Type: Binding Request (0x0001)
    msg[0] = 0x00; msg[1] = 0x01;
    // Message Length: 0 (no attributes in request)
    msg[2] = 0x00; msg[3] = 0x00;
    // Magic Cookie (0x2112A442)
    msg[4] = 0x21; msg[5] = 0x12; msg[6] = 0xA4; msg[7] = 0x42;
    // Transaction ID (12 random bytes)
    for (size_t i = 8; i < kStunHeaderSize; ++i) {
        msg[i] = static_cast<uint8_t>(dist(rng));
    }
    return msg;
}

// ─── Parse Binding Response ──────────────────────────────────────────────────

std::optional<Endpoint> StunClient::parse_binding_response(
    const std::vector<uint8_t>& data,
    const std::vector<uint8_t>& transaction_id) const {

    if (data.size() < kStunHeaderSize) return std::nullopt;

    // Verify response type
    uint16_t msg_type = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    if (msg_type != kStunBindingResponse) return std::nullopt;

    // Verify magic cookie
    uint32_t cookie = (static_cast<uint32_t>(data[4]) << 24) |
                      (static_cast<uint32_t>(data[5]) << 16) |
                      (static_cast<uint32_t>(data[6]) << 8) |
                      data[7];
    if (cookie != kStunMagicCookie) return std::nullopt;

    // Verify transaction ID
    if (std::memcmp(data.data() + 8, transaction_id.data(), kTransactionIdSize) != 0)
        return std::nullopt;

    // Parse message length
    uint16_t msg_len = (static_cast<uint16_t>(data[2]) << 8) | data[3];
    if (data.size() < kStunHeaderSize + msg_len) return std::nullopt;

    // Parse attributes: prefer XOR-MAPPED-ADDRESS, fallback to MAPPED-ADDRESS
    std::optional<Endpoint> xor_mapped, plain_mapped;
    size_t offset = kStunHeaderSize;

    while (offset + 4 <= kStunHeaderSize + msg_len) {
        uint16_t attr_type = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
        uint16_t attr_len  = (static_cast<uint16_t>(data[offset + 2]) << 8) | data[offset + 3];
        offset += 4;

        if (offset + attr_len > data.size()) break;

        if (attr_type == kAttrXorMappedAddr && attr_len >= 8) {
            uint8_t family = data[offset + 1];
            if (family == 0x01) { // IPv4
                uint16_t xor_port = (static_cast<uint16_t>(data[offset + 2]) << 8) |
                                    data[offset + 3];
                uint16_t port = xor_port ^ static_cast<uint16_t>(kStunMagicCookie >> 16);

                uint32_t xor_ip = (static_cast<uint32_t>(data[offset + 4]) << 24) |
                                  (static_cast<uint32_t>(data[offset + 5]) << 16) |
                                  (static_cast<uint32_t>(data[offset + 6]) << 8) |
                                  data[offset + 7];
                uint32_t ip_raw = xor_ip ^ kStunMagicCookie;

                char ip_str[INET_ADDRSTRLEN]{};
                uint32_t ip_net = htonl(ip_raw);
                inet_ntop(AF_INET, &ip_net, ip_str, sizeof(ip_str));
                xor_mapped = Endpoint{ip_str, port};
            }
        } else if (attr_type == kAttrMappedAddress && attr_len >= 8) {
            uint8_t family = data[offset + 1];
            if (family == 0x01) { // IPv4
                uint16_t port = (static_cast<uint16_t>(data[offset + 2]) << 8) |
                                data[offset + 3];
                char ip_str[INET_ADDRSTRLEN]{};
                uint32_t ip_net = (static_cast<uint32_t>(data[offset + 4]) << 24) |
                                  (static_cast<uint32_t>(data[offset + 5]) << 16) |
                                  (static_cast<uint32_t>(data[offset + 6]) << 8) |
                                  data[offset + 7];
                ip_net = htonl(ip_net);
                inet_ntop(AF_INET, &ip_net, ip_str, sizeof(ip_str));
                plain_mapped = Endpoint{ip_str, port};
            }
        }

        // Attributes are padded to 4-byte boundary
        offset += attr_len;
        if (attr_len % 4 != 0) offset += (4 - attr_len % 4);
    }

    return xor_mapped ? xor_mapped : plain_mapped;
}

// ─── Resolve Hostname ────────────────────────────────────────────────────────

std::string StunClient::resolve_host(const std::string& hostname) const {
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(hostname.c_str(), nullptr, &hints, &res) != 0 || !res) {
        return {};
    }

    char ip[INET_ADDRSTRLEN]{};
    inet_ntop(AF_INET,
              &reinterpret_cast<sockaddr_in*>(res->ai_addr)->sin_addr,
              ip, sizeof(ip));
    freeaddrinfo(res);
    return ip;
}

// ─── Single STUN Query ───────────────────────────────────────────────────────

StunResult StunClient::query(UdpSocket& /*socket*/, const std::string& server,
                             uint16_t port, int timeout_ms) {
    StunResult result;

    std::string server_ip = resolve_host(server);
    if (server_ip.empty()) return result;

    Endpoint stun_ep{server_ip, port};

    // Use a dedicated socket for STUN to avoid interfering with the main receive loop
    UdpSocket stun_socket;
    auto bound = stun_socket.bind(0);
    if (!bound) return result;

    auto request = build_binding_request();
    std::vector<uint8_t> txn_id(request.begin() + 8, request.begin() + 20);

    // Send with retransmission (RFC 5389 recommends exponential backoff)
    constexpr int kMaxRetries = 3;
    std::atomic<bool> got_response{false};
    std::vector<uint8_t> response_data;
    std::mutex response_mtx;

    stun_socket.start_receive([&](Datagram dg) {
        std::lock_guard<std::mutex> lock(response_mtx);
        if (!got_response) {
            response_data = std::move(dg.data);
            got_response = true;
        }
    });

    int rto = timeout_ms / (kMaxRetries * 2);
    if (rto < 200) rto = 200;

    for (int attempt = 0; attempt < kMaxRetries && !got_response; ++attempt) {
        (void)stun_socket.send_to(request, stun_ep);

        auto attempt_start = Clock::now();
        while (!got_response) {
            auto elapsed = std::chrono::duration_cast<Millis>(Clock::now() - attempt_start).count();
            if (elapsed >= rto) break;
            std::this_thread::sleep_for(Millis(20));
        }
        rto *= 2; // Exponential backoff
    }

    stun_socket.stop_receive();
    stun_socket.close();

    if (got_response) {
        std::lock_guard<std::mutex> lock(response_mtx);
        auto mapped = parse_binding_response(response_data, txn_id);
        if (mapped) {
            result.success = true;
            result.mapped_ep = *mapped;
        }
    }

    return result;
}

// ─── NAT Type Detection ──────────────────────────────────────────────────────

StunResult StunClient::detect_nat_type(UdpSocket& socket,
                                        const std::string& server1, uint16_t port1,
                                        const std::string& server2, uint16_t port2,
                                        int timeout_ms) {
    // Step 1: Query first STUN server
    auto result1 = query(socket, server1, port1, timeout_ms);
    if (!result1.success) {
        return result1;
    }

    // Step 2: Check if mapped address equals local address (Open Internet)
    auto local_ip = UdpSocket::get_local_ip();
    if (result1.mapped_ep.ip == local_ip) {
        result1.nat_type = NatType::OpenInternet;
        return result1;
    }

    // Step 3: Query second STUN server to detect Symmetric NAT
    auto result2 = query(socket, server2, port2, timeout_ms);
    if (!result2.success) {
        // Cannot reach second server; conservative classification
        result1.nat_type = NatType::PortRestricted;
        return result1;
    }

    // Step 4: Compare mappings
    if (result1.mapped_ep.ip != result2.mapped_ep.ip ||
        result1.mapped_ep.port != result2.mapped_ep.port) {
        // Different mapping per destination = Symmetric NAT
        result1.nat_type = NatType::Symmetric;
    } else {
        // Same mapping = some form of Cone NAT.
        // Full classification requires RFC 5780 (CHANGE-REQUEST attribute).
        // Without it, we classify as PortRestricted (most common consumer NAT).
        result1.nat_type = NatType::PortRestricted;
    }

    return result1;
}

} // namespace p2p
