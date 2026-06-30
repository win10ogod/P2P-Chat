#include "protocol/protocol.h"

namespace p2p {

// ─── Packet Serialization ────────────────────────────────────────────────────

std::vector<uint8_t> Packet::serialize() const {
    std::vector<uint8_t> out;
    out.reserve(PacketHeader::kSize + payload.size());

    // Magic (4 bytes, big-endian)
    out.push_back(static_cast<uint8_t>((header.magic >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((header.magic >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((header.magic >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(header.magic & 0xFF));
    // Type
    out.push_back(static_cast<uint8_t>(header.type));
    // Flags
    out.push_back(header.flags);
    // SeqNo (4 bytes)
    out.push_back(static_cast<uint8_t>((header.seq_no >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((header.seq_no >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((header.seq_no >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(header.seq_no & 0xFF));
    // PayloadLen (2 bytes)
    auto plen = static_cast<uint16_t>(payload.size());
    out.push_back(static_cast<uint8_t>((plen >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(plen & 0xFF));
    // Payload
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::optional<Packet> Packet::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < PacketHeader::kSize) return std::nullopt;

    Packet pkt;
    size_t i = 0;

    pkt.header.magic = (uint32_t(data[i]) << 24) | (uint32_t(data[i+1]) << 16) |
                       (uint32_t(data[i+2]) << 8) | data[i+3];
    i += 4;
    if (pkt.header.magic != config::kMagicNumber) return std::nullopt;

    pkt.header.type = static_cast<PacketType>(data[i++]);
    pkt.header.flags = data[i++];

    pkt.header.seq_no = (uint32_t(data[i]) << 24) | (uint32_t(data[i+1]) << 16) |
                        (uint32_t(data[i+2]) << 8) | data[i+3];
    i += 4;

    pkt.header.payload_len = static_cast<uint16_t>((uint16_t(data[i]) << 8) | data[i+1]);
    i += 2;

    if (data.size() < i + pkt.header.payload_len) return std::nullopt;
    pkt.payload.assign(data.begin() + static_cast<ptrdiff_t>(i),
                       data.begin() + static_cast<ptrdiff_t>(i + pkt.header.payload_len));
    return pkt;
}

// ─── Packet Factory ──────────────────────────────────────────────────────────

namespace PacketFactory {

static Packet make(PacketType type, uint32_t seq, std::vector<uint8_t> payload) {
    Packet p;
    p.header.type = type;
    p.header.seq_no = seq;
    p.header.payload_len = static_cast<uint16_t>(payload.size());
    p.payload = std::move(payload);
    return p;
}

Packet heartbeat(uint32_t seq)     { return make(PacketType::Heartbeat, seq, {}); }
Packet heartbeat_ack(uint32_t seq) { return make(PacketType::HeartbeatAck, seq, {}); }
Packet make_disconnect()           { return make(PacketType::Disconnect, 0, {}); }

Packet make_register(const std::string& name, const std::string& uuid,
                     const Endpoint& local_ep, uint16_t port) {
    ByteWriter w;
    w.write_string(name);
    w.write_string(uuid);
    w.write_endpoint(local_ep);
    w.write_u16(port);
    return make(PacketType::Register, 0, w.take());
}

Packet make_register_ack(bool ok, const std::string& msg, const Endpoint& pub_ep) {
    ByteWriter w;
    w.write_u8(ok ? 1 : 0);
    w.write_string(msg);
    w.write_endpoint(pub_ep);
    return make(PacketType::RegisterAck, 0, w.take());
}

Packet make_peer_list(const std::vector<PeerEntry>& peers) {
    ByteWriter w;
    w.write_u16(static_cast<uint16_t>(peers.size()));
    for (const auto& p : peers) {
        w.write_string(p.name);
        w.write_string(p.uuid);
        w.write_endpoint(p.pub_ep);
        w.write_endpoint(p.local_ep);
    }
    return make(PacketType::PeerList, 0, w.take());
}

Packet make_connect_request(const std::string& from, const std::string& to) {
    ByteWriter w;
    w.write_string(from);
    w.write_string(to);
    return make(PacketType::ConnectRequest, 0, w.take());
}

Packet make_punch_notify(const std::string& uuid, const std::string& name,
                         const Endpoint& pub, const Endpoint& local) {
    ByteWriter w;
    w.write_string(uuid);
    w.write_string(name);
    w.write_endpoint(pub);
    w.write_endpoint(local);
    return make(PacketType::PunchNotify, 0, w.take());
}

Packet make_punch_probe(const std::string& uuid) {
    ByteWriter w;
    w.write_string(uuid);
    return make(PacketType::PunchProbe, 0, w.take());
}

Packet make_punch_ack(const std::string& uuid) {
    ByteWriter w;
    w.write_string(uuid);
    return make(PacketType::PunchAck, 0, w.take());
}

Packet make_text(const std::string& sender, const std::string& text, uint32_t id) {
    ByteWriter w;
    w.write_string(sender);
    w.write_string(text);
    w.write_u32(id);
    return make(PacketType::TextMessage, id, w.take());
}

Packet make_text_ack(uint32_t id) {
    return make(PacketType::TextMessageAck, id, {});
}

Packet make_audio(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus) {
    ByteWriter w;
    w.write_u32(seq);
    w.write_u64(ts);
    w.write_bytes(opus);
    return make(PacketType::AudioFrame, seq, w.take());
}

} // namespace PacketFactory

// ─── Packet Parser ───────────────────────────────────────────────────────────

namespace PacketParser {

std::optional<RegisterData> parse_register(const Packet& p) {
    if (p.header.type != PacketType::Register) return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(4)) return std::nullopt; // Minimum: two 2-byte length prefixes
    RegisterData d;
    d.name = r.read_string();
    d.uuid = r.read_string();
    d.local_ep = r.read_endpoint();
    if (!r.has(2)) return std::nullopt;
    d.port = r.read_u16();
    return d;
}

std::optional<RegisterAckData> parse_register_ack(const Packet& p) {
    if (p.header.type != PacketType::RegisterAck) return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(1)) return std::nullopt;
    RegisterAckData d;
    d.ok = r.read_u8() != 0;
    d.msg = r.read_string();
    d.pub_ep = r.read_endpoint();
    return d;
}

std::optional<PeerListData> parse_peer_list(const Packet& p) {
    if (p.header.type != PacketType::PeerList) return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(2)) return std::nullopt;
    PeerListData d;
    uint16_t count = r.read_u16();
    for (uint16_t i = 0; i < count; ++i) {
        if (!r.has(2)) return std::nullopt;
        PacketFactory::PeerEntry e;
        e.name = r.read_string();
        e.uuid = r.read_string();
        e.pub_ep = r.read_endpoint();
        e.local_ep = r.read_endpoint();
        d.peers.push_back(std::move(e));
    }
    return d;
}

std::optional<PunchNotifyData> parse_punch_notify(const Packet& p) {
    if (p.header.type != PacketType::PunchNotify) return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(2)) return std::nullopt;
    PunchNotifyData d;
    d.uuid = r.read_string();
    d.name = r.read_string();
    d.pub_ep = r.read_endpoint();
    d.local_ep = r.read_endpoint();
    return d;
}

std::optional<PunchData> parse_punch(const Packet& p) {
    if (p.header.type != PacketType::PunchProbe && p.header.type != PacketType::PunchAck)
        return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(2)) return std::nullopt;
    return PunchData{r.read_string()};
}

std::optional<TextData> parse_text(const Packet& p) {
    if (p.header.type != PacketType::TextMessage) return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(2)) return std::nullopt;
    TextData d;
    d.sender = r.read_string();
    d.text = r.read_string();
    if (!r.has(4)) return std::nullopt;
    d.id = r.read_u32();
    return d;
}

std::optional<AudioData> parse_audio(const Packet& p) {
    if (p.header.type != PacketType::AudioFrame) return std::nullopt;
    ByteReader r(p.payload);
    if (!r.has(14)) return std::nullopt; // 4 + 8 + 2 minimum
    AudioData d;
    d.seq = r.read_u32();
    d.ts = r.read_u64();
    d.opus = r.read_bytes();
    return d;
}

} // namespace PacketParser
} // namespace p2p
