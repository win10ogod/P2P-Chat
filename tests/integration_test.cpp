/**
 * @file integration_test.cpp
 * @brief Integration test: two headless clients connect via signaling server
 *        and exchange text messages over P2P.
 *
 * Test flow:
 *   1. Client A and Client B both register with the signaling server.
 *   2. Client A requests a P2P connection to Client B.
 *   3. Both clients perform NAT hole-punching (local loopback in this test).
 *   4. Client A sends a text message to Client B.
 *   5. Client B receives the message and sends a reply.
 *   6. Client A receives the reply.
 *   7. Both clients disconnect gracefully.
 */

#include "core/session.h"
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

using namespace p2p;

static constexpr uint16_t kServerPort = 9100; // Use non-default port for testing
static constexpr int kTimeoutSec = 10;

/// Simple test assertion macro
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::cerr << "[FAIL] " << msg << " (line " << __LINE__ << ")\n"; \
        return 1; \
    } \
} while(0)

/// Wait for a condition with timeout
template<typename Pred>
bool wait_for(Pred pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return true;
}

int main() {
    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║        P2P Chat Integration Test - Two Clients              ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n\n";

    // ─── Step 1: Initialize both sessions ────────────────────────────────────
    std::cout << "[TEST] Step 1: Initializing Client A and Client B...\n";

    Session clientA;
    Session clientB;

    TEST_ASSERT(clientA.init("Alice", 0), "Client A init failed");
    TEST_ASSERT(clientB.init("Bob", 0), "Client B init failed");

    std::cout << "  Client A: " << clientA.local_id().name
              << " (UUID: " << clientA.local_id().uuid.substr(0, 8) << "...)"
              << " on port " << clientA.local_ep().port << "\n";
    std::cout << "  Client B: " << clientB.local_id().name
              << " (UUID: " << clientB.local_id().uuid.substr(0, 8) << "...)"
              << " on port " << clientB.local_ep().port << "\n";
    std::cout << "  [OK] Both clients initialized.\n\n";

    // ─── Step 2: Connect both to signaling server ────────────────────────────
    std::cout << "[TEST] Step 2: Connecting to signaling server (127.0.0.1:" << kServerPort << ")...\n";

    clientA.connect_server("127.0.0.1", kServerPort);
    clientB.connect_server("127.0.0.1", kServerPort);

    // Wait for both to receive ServerConnected event
    std::atomic<bool> a_registered{false}, b_registered{false};

    auto drain_events = [](Session& s, std::atomic<bool>& registered,
                           std::vector<ChatEvent>& events) {
        while (auto ev = s.poll_event()) {
            events.push_back(*ev);
            if (ev->type == ChatEvent::ServerConnected) registered = true;
        }
    };

    std::vector<ChatEvent> eventsA, eventsB;

    bool server_ok = wait_for([&] {
        drain_events(clientA, a_registered, eventsA);
        drain_events(clientB, b_registered, eventsB);
        return a_registered.load() && b_registered.load();
    }, 5000);

    TEST_ASSERT(server_ok, "Clients failed to register with signaling server");
    std::cout << "  [OK] Both clients registered with server.\n\n";

    // ─── Step 3: Wait for peer list update ───────────────────────────────────
    std::cout << "[TEST] Step 3: Waiting for peer discovery...\n";

    bool peers_found = wait_for([&] {
        drain_events(clientA, a_registered, eventsA);
        drain_events(clientB, b_registered, eventsB);
        auto peersA = clientA.known_peers();
        auto peersB = clientB.known_peers();
        return peersA.size() >= 2 && peersB.size() >= 2;
    }, 5000);

    TEST_ASSERT(peers_found, "Peer discovery failed");

    auto peersA = clientA.known_peers();
    std::cout << "  Client A sees " << peersA.size() << " peers:\n";
    for (const auto& p : peersA) {
        std::cout << "    - " << p.name << " (" << p.uuid.substr(0, 8) << "...) "
                  << p.pub_ep.to_string() << "\n";
    }
    std::cout << "  [OK] Peers discovered.\n\n";

    // ─── Step 4: Client A connects to Client B via P2P ──────────────────────
    std::cout << "[TEST] Step 4: Client A initiating P2P connection to Client B...\n";

    clientA.connect_peer(clientB.local_id().uuid);

    // Wait for PeerConnected event on both sides
    std::atomic<bool> a_connected{false}, b_connected{false};

    bool p2p_ok = wait_for([&] {
        while (auto ev = clientA.poll_event()) {
            eventsA.push_back(*ev);
            if (ev->type == ChatEvent::PeerConnected) a_connected = true;
        }
        while (auto ev = clientB.poll_event()) {
            eventsB.push_back(*ev);
            if (ev->type == ChatEvent::PeerConnected) b_connected = true;
        }
        return a_connected.load() && b_connected.load();
    }, 8000);

    TEST_ASSERT(p2p_ok, "P2P connection establishment failed");
    TEST_ASSERT(clientA.is_connected(clientB.local_id().uuid), "A not connected to B");
    TEST_ASSERT(clientB.is_connected(clientA.local_id().uuid), "B not connected to A");
    std::cout << "  [OK] P2P connection established between Alice and Bob.\n\n";

    // ─── Step 5: Client A sends text to Client B ─────────────────────────────
    std::cout << "[TEST] Step 5: Alice sends message to Bob...\n";

    const std::string msg_a_to_b = "Hello Bob! This is a P2P message from Alice.";
    bool sent = clientA.send_text(clientB.local_id().uuid, msg_a_to_b);
    TEST_ASSERT(sent, "Client A failed to send text");

    // Wait for Client B to receive it
    std::string received_by_b;
    bool b_got_msg = wait_for([&] {
        while (auto ev = clientB.poll_event()) {
            eventsB.push_back(*ev);
            if (ev->type == ChatEvent::TextReceived) {
                received_by_b = ev->text;
                return true;
            }
        }
        return false;
    }, 5000);

    TEST_ASSERT(b_got_msg, "Client B did not receive message from A");
    TEST_ASSERT(received_by_b == msg_a_to_b, "Message content mismatch on B");
    std::cout << "  Bob received: \"" << received_by_b << "\"\n";
    std::cout << "  [OK] Message delivered successfully.\n\n";

    // ─── Step 6: Client B replies to Client A ────────────────────────────────
    std::cout << "[TEST] Step 6: Bob sends reply to Alice...\n";

    const std::string msg_b_to_a = "Hi Alice! Got your message. P2P works great!";
    sent = clientB.send_text(clientA.local_id().uuid, msg_b_to_a);
    TEST_ASSERT(sent, "Client B failed to send text");

    std::string received_by_a;
    bool a_got_msg = wait_for([&] {
        while (auto ev = clientA.poll_event()) {
            eventsA.push_back(*ev);
            if (ev->type == ChatEvent::TextReceived) {
                received_by_a = ev->text;
                return true;
            }
        }
        return false;
    }, 5000);

    TEST_ASSERT(a_got_msg, "Client A did not receive reply from B");
    TEST_ASSERT(received_by_a == msg_b_to_a, "Message content mismatch on A");
    std::cout << "  Alice received: \"" << received_by_a << "\"\n";
    std::cout << "  [OK] Reply delivered successfully.\n\n";

    // ─── Step 7: Graceful disconnect ─────────────────────────────────────────
    std::cout << "[TEST] Step 7: Disconnecting...\n";

    clientA.disconnect_peer(clientB.local_id().uuid);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    TEST_ASSERT(!clientA.is_connected(clientB.local_id().uuid), "A still connected after disconnect");
    std::cout << "  [OK] Disconnected gracefully.\n\n";

    // ─── Shutdown ────────────────────────────────────────────────────────────
    clientA.shutdown();
    clientB.shutdown();

    std::cout << "╔══════════════════════════════════════════════════════════════╗\n";
    std::cout << "║                ALL TESTS PASSED                             ║\n";
    std::cout << "╚══════════════════════════════════════════════════════════════╝\n";
    return 0;
}
