#pragma once

/**
 * @file protocol.h
 * @brief Binary wire protocol for P2P Chat.
 *
 * Packet layout: [Magic 4B][Type 1B][Flags 1B][SeqNo 4B][PayloadLen 2B][Payload...]
 * Total header size: 12 bytes. All multi-byte integers are big-endian.
 */

#include "core/types.h"
#include <optional>
#include <vector>

namespace p2p {

// ─── Packet Header & Packet ──────────────────────────────────────────────────

struct PacketHeader {
    uint32_t   magic{config::kMagicNumber};
    PacketType type{PacketType::Heartbeat};
    uint8_t    flags{0};
    uint32_t   seq_no{0};
    uint16_t   payload_len{0};

    static constexpr size_t kSize = 12;
};

struct Packet {
    PacketHeader         header;
    std::vector<uint8_t> payload;

    [[nodiscard]] std::vector<uint8_t> serialize() const;
    [[nodiscard]] static std::optional<Packet> deserialize(const std::vector<uint8_t>& data);
};

// ─── Serialization Helpers ───────────────────────────────────────────────────

class ByteWriter {
public:
    void write_u8(uint8_t v) { buf_.push_back(v); }

    void write_u16(uint16_t v) {
        buf_.push_back(static_cast<uint8_t>(v >> 8));
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void write_u32(uint32_t v) {
        buf_.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
        buf_.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        buf_.push_back(static_cast<uint8_t>(v & 0xFF));
    }

    void write_u64(uint64_t v) {
        write_u32(static_cast<uint32_t>(v >> 32));
        write_u32(static_cast<uint32_t>(v));
    }

    void write_string(const std::string& s) {
        write_u16(static_cast<uint16_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }

    void write_bytes(const std::vector<uint8_t>& v) {
        write_u16(static_cast<uint16_t>(v.size()));
        buf_.insert(buf_.end(), v.begin(), v.end());
    }

    void write_endpoint(const Endpoint& ep) {
        write_string(ep.ip);
        write_u16(ep.port);
    }

    [[nodiscard]] std::vector<uint8_t> take() { return std::move(buf_); }

private:
    std::vector<uint8_t> buf_;
};

/**
 * @class ByteReader
 * @brief Safe binary reader with bounds checking.
 */
class ByteReader {
public:
    explicit ByteReader(const std::vector<uint8_t>& v)
        : data_(v.data()), len_(v.size()), pos_(0) {}

    [[nodiscard]] bool   has(size_t n)       const noexcept { return pos_ + n <= len_; }
    [[nodiscard]] size_t remaining()         const noexcept { return len_ - pos_; }
    [[nodiscard]] bool   is_valid()          const noexcept { return valid_; }

    uint8_t read_u8() {
        if (!has(1)) { valid_ = false; return 0; }
        return data_[pos_++];
    }

    uint16_t read_u16() {
        if (!has(2)) { valid_ = false; return 0; }
        uint16_t v = (static_cast<uint16_t>(data_[pos_]) << 8) | data_[pos_ + 1];
        pos_ += 2;
        return v;
    }

    uint32_t read_u32() {
        if (!has(4)) { valid_ = false; return 0; }
        uint32_t v = (static_cast<uint32_t>(data_[pos_]) << 24) |
                     (static_cast<uint32_t>(data_[pos_ + 1]) << 16) |
                     (static_cast<uint32_t>(data_[pos_ + 2]) << 8) |
                     data_[pos_ + 3];
        pos_ += 4;
        return v;
    }

    uint64_t read_u64() {
        uint64_t hi = read_u32();
        uint64_t lo = read_u32();
        return (hi << 32) | lo;
    }

    std::string read_string() {
        uint16_t len = read_u16();
        if (!valid_ || !has(len)) { valid_ = false; return {}; }
        std::string s(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        return s;
    }

    std::vector<uint8_t> read_bytes() {
        uint16_t len = read_u16();
        if (!valid_ || !has(len)) { valid_ = false; return {}; }
        std::vector<uint8_t> v(data_ + pos_, data_ + pos_ + len);
        pos_ += len;
        return v;
    }

    Endpoint read_endpoint() {
        auto ip = read_string();
        auto port = read_u16();
        return {std::move(ip), port};
    }

private:
    const uint8_t* data_;
    size_t         len_;
    size_t         pos_;
    bool           valid_{true};
};

// ─── Packet Factory ──────────────────────────────────────────────────────────

namespace PacketFactory {

struct PeerEntry {
    std::string name;
    std::string uuid;
    Endpoint    pub_ep;
    Endpoint    local_ep;
};

Packet heartbeat(uint32_t seq = 0);
Packet heartbeat_ack(uint32_t seq = 0);
Packet make_disconnect();
Packet make_register(const std::string& name, const std::string& uuid,
                     const Endpoint& local_ep, uint16_t port);
Packet make_register_ack(bool ok, const std::string& msg, const Endpoint& pub_ep);
Packet make_peer_list(const std::vector<PeerEntry>& peers);
Packet make_connect_request(const std::string& from, const std::string& to);
Packet make_punch_notify(const std::string& uuid, const std::string& name,
                         const Endpoint& pub, const Endpoint& local);
Packet make_punch_probe(const std::string& uuid);
Packet make_punch_ack(const std::string& uuid);
Packet make_text(const std::string& sender, const std::string& text, uint32_t id);
Packet make_text_ack(uint32_t id);
Packet make_audio(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus);

} // namespace PacketFactory

// ─── Packet Parser ───────────────────────────────────────────────────────────

namespace PacketParser {

struct RegisterData    { std::string name, uuid; Endpoint local_ep; uint16_t port; };
struct RegisterAckData { bool ok; std::string msg; Endpoint pub_ep; };
struct PeerListData    { std::vector<PacketFactory::PeerEntry> peers; };
struct PunchNotifyData { std::string uuid, name; Endpoint pub_ep, local_ep; };
struct PunchData       { std::string uuid; };
struct TextData        { std::string sender, text; uint32_t id; };
struct AudioData       { uint32_t seq; uint64_t ts; std::vector<uint8_t> opus; };

std::optional<RegisterData>    parse_register(const Packet& p);
std::optional<RegisterAckData> parse_register_ack(const Packet& p);
std::optional<PeerListData>    parse_peer_list(const Packet& p);
std::optional<PunchNotifyData> parse_punch_notify(const Packet& p);
std::optional<PunchData>       parse_punch(const Packet& p);
std::optional<TextData>        parse_text(const Packet& p);
std::optional<AudioData>       parse_audio(const Packet& p);

} // namespace PacketParser

} // namespace p2p
