#include "core/types.h"
#include <random>
#include <sstream>
#include <iomanip>

namespace p2p {

std::string generate_uuid() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    static thread_local std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

    auto rand_hex = [&](int len) {
        std::ostringstream oss;
        uint32_t val = dist(rng);
        oss << std::hex << std::setfill('0') << std::setw(len) << (val & ((1ULL << (len * 4)) - 1));
        return oss.str();
    };

    // UUID v4 format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    std::ostringstream uuid;
    uuid << rand_hex(8) << "-"
         << rand_hex(4) << "-4"
         << rand_hex(3) << "-"
         << rand_hex(4) << "-"
         << rand_hex(8) << rand_hex(4);
    return uuid.str();
}

} // namespace p2p
