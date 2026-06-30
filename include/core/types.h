#pragma once

/**
 * @file types.h
 * @brief Core type definitions and compile-time constants for the P2P Chat platform.
 *
 * This header provides fundamental data types, enumerations, and compile-time
 * constants used throughout the application. All types follow value semantics
 * and modern C++17 idioms.
 */

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace p2p {

// ─── Time Utilities ──────────────────────────────────────────────────────────

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Duration  = Clock::duration;
using Millis    = std::chrono::milliseconds;

[[nodiscard]] inline uint64_t now_ms() noexcept {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<Millis>(Clock::now().time_since_epoch()).count());
}

// ─── Configuration Constants ─────────────────────────────────────────────────

namespace config {
    inline constexpr uint16_t kDefaultSignalingPort = 9000;
    inline constexpr size_t   kMaxUdpPayload        = 1400;
    inline constexpr size_t   kMaxPacketSize         = 65535;
    inline constexpr uint32_t kMagicNumber           = 0x50325043; // "P2PC"
    inline constexpr uint32_t kHeartbeatIntervalMs   = 5000;
    inline constexpr uint32_t kConnectionTimeoutMs   = 30000;
    inline constexpr uint32_t kPunchIntervalMs       = 500;
    inline constexpr uint32_t kPunchMaxAttempts      = 20;
    inline constexpr uint32_t kPeerStaleTimeoutMs    = 60000;
}

// ─── Audio Configuration ─────────────────────────────────────────────────────

namespace audio_config {
    inline constexpr int kSampleRate      = 48000;
    inline constexpr int kChannels        = 1;
    inline constexpr int kFrameSize       = 960;   // 20ms at 48kHz
    inline constexpr int kBitrate         = 64000;  // 64 kbps
    inline constexpr int kJitterBufFrames = 10;
}

// ─── Endpoint (IP:Port pair) ─────────────────────────────────────────────────

struct Endpoint {
    std::string ip;
    uint16_t    port{0};

    Endpoint() = default;
    Endpoint(std::string ip_, uint16_t port_) : ip(std::move(ip_)), port(port_) {}

    [[nodiscard]] bool is_valid() const noexcept {
        return !ip.empty() && port != 0;
    }

    [[nodiscard]] std::string to_string() const {
        return ip + ":" + std::to_string(port);
    }

    bool operator==(const Endpoint& o) const noexcept { return ip == o.ip && port == o.port; }
    bool operator!=(const Endpoint& o) const noexcept { return !(*this == o); }
    bool operator<(const Endpoint& o) const noexcept {
        return std::tie(ip, port) < std::tie(o.ip, o.port);
    }
};

// ─── Peer Identity ───────────────────────────────────────────────────────────

struct PeerId {
    std::string name;
    std::string uuid;

    [[nodiscard]] bool is_valid() const noexcept { return !uuid.empty(); }

    bool operator==(const PeerId& o) const noexcept { return uuid == o.uuid; }
    bool operator!=(const PeerId& o) const noexcept { return !(*this == o); }
    bool operator<(const PeerId& o) const noexcept  { return uuid < o.uuid; }
};

// ─── Connection State Machine ────────────────────────────────────────────────

enum class ConnState : uint8_t {
    Disconnected = 0,
    Connecting,
    Punching,
    Connected,
    Failed
};

[[nodiscard]] inline std::string_view conn_state_str(ConnState s) noexcept {
    switch (s) {
        case ConnState::Disconnected: return "Disconnected";
        case ConnState::Connecting:   return "Connecting";
        case ConnState::Punching:     return "Punching";
        case ConnState::Connected:    return "Connected";
        case ConnState::Failed:       return "Failed";
    }
    return "Unknown";
}

// ─── Packet Types ────────────────────────────────────────────────────────────

enum class PacketType : uint8_t {
    // Control
    Heartbeat       = 0x01,
    HeartbeatAck    = 0x02,
    Disconnect      = 0x03,

    // Signaling
    Register        = 0x10,
    RegisterAck     = 0x11,
    PeerList        = 0x12,
    ConnectRequest  = 0x13,
    ConnectResponse = 0x14,
    PunchNotify     = 0x15,

    // Data
    TextMessage     = 0x20,
    TextMessageAck  = 0x21,

    // Audio
    AudioFrame      = 0x30,

    // NAT traversal
    PunchProbe      = 0x40,
    PunchAck        = 0x41,

    // Relay (TURN-like)
    RelayAllocate   = 0x50,
    RelayAllocateOk = 0x51,
    RelayData       = 0x52,
    RelayRelease    = 0x53,
};

// ─── Thread-safe Bounded Queue ───────────────────────────────────────────────

/**
 * @class ConcurrentQueue
 * @brief A thread-safe, bounded, FIFO queue for inter-thread communication.
 */
template<typename T>
class ConcurrentQueue {
public:
    explicit ConcurrentQueue(size_t max_size = 1024) : max_size_(max_size) {}

    /// Push an item. Returns false if the queue is full.
    bool push(T item) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.size() >= max_size_) return false;
        queue_.push_back(std::move(item));
        cv_.notify_one();
        return true;
    }

    /// Try to pop an item without blocking.
    [[nodiscard]] std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mtx_);
        if (queue_.empty()) return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    /// Wait up to `timeout` for an item.
    [[nodiscard]] std::optional<T> wait_pop(Millis timeout) {
        std::unique_lock<std::mutex> lock(mtx_);
        if (!cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); }))
            return std::nullopt;
        T item = std::move(queue_.front());
        queue_.pop_front();
        return item;
    }

    [[nodiscard]] size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.clear();
    }

private:
    mutable std::mutex      mtx_;
    std::condition_variable cv_;
    std::deque<T>           queue_;
    size_t                  max_size_;
};

// ─── UUID Generator ──────────────────────────────────────────────────────────

[[nodiscard]] std::string generate_uuid();

} // namespace p2p
