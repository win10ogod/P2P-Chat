#include "network/turn_client.h"

namespace p2p {

TurnClient::TurnClient(UdpSocket& socket) : socket_(socket) {}

TurnClient::~TurnClient() { release(); }

bool TurnClient::allocate(const Endpoint& relay_server, const std::string& my_uuid) {
    if (state_ != RelayState::Idle && state_ != RelayState::Failed) return false;

    server_ep_ = relay_server;
    my_uuid_ = my_uuid;
    state_ = RelayState::Allocating;

    // Build and send RelayAllocate request
    Packet pkt;
    pkt.header.type = PacketType::RelayAllocate;
    ByteWriter w;
    w.write_string(my_uuid_);
    pkt.payload = w.take();
    pkt.header.payload_len = static_cast<uint16_t>(pkt.payload.size());

    auto data = pkt.serialize();
    int sent = socket_.send_to(data, server_ep_);
    if (sent <= 0) {
        state_ = RelayState::Failed;
        return false;
    }

    return true;
}

bool TurnClient::send_relayed(const std::string& target_uuid, const std::vector<uint8_t>& data) {
    if (state_ != RelayState::Active) return false;

    // Wrap data in a RelayData packet with source and target UUID
    Packet pkt;
    pkt.header.type = PacketType::RelayData;
    ByteWriter w;
    w.write_string(my_uuid_);
    w.write_string(target_uuid);
    w.write_bytes(data);
    pkt.payload = w.take();
    pkt.header.payload_len = static_cast<uint16_t>(pkt.payload.size());

    auto serialized = pkt.serialize();
    int sent = socket_.send_to(serialized, server_ep_);
    return sent > 0;
}

void TurnClient::handle_packet(const Packet& pkt, const Endpoint& /*from*/) {
    switch (pkt.header.type) {
    case PacketType::RelayAllocateOk: {
        // Server confirms allocation and provides relay endpoint info
        ByteReader r(pkt.payload);
        auto relay_ip = r.read_string();
        auto relay_port = r.read_u16();
        if (r.is_valid()) {
            relay_ep_ = Endpoint{relay_ip, relay_port};
            state_ = RelayState::Active;
        } else {
            state_ = RelayState::Failed;
        }
        break;
    }
    case PacketType::RelayData: {
        // Incoming relayed data from another peer via the server
        ByteReader r(pkt.payload);
        auto from_uuid = r.read_string();
        auto _target = r.read_string();
        (void)_target;
        auto payload = r.read_bytes();
        if (r.is_valid() && data_cb_) {
            data_cb_(from_uuid, payload);
        }
        break;
    }
    default:
        break;
    }
}

void TurnClient::release() {
    if (state_ == RelayState::Active) {
        Packet pkt;
        pkt.header.type = PacketType::RelayRelease;
        ByteWriter w;
        w.write_string(my_uuid_);
        pkt.payload = w.take();
        pkt.header.payload_len = static_cast<uint16_t>(pkt.payload.size());

        auto data = pkt.serialize();
        (void)socket_.send_to(data, server_ep_);
    }
    state_ = RelayState::Idle;
    relay_ep_ = {};
}

} // namespace p2p
