# P2P Chat

A cross-platform peer-to-peer chat application built with C++17, featuring a graphical user interface powered by Dear ImGui, real-time text messaging over direct UDP connections, optional voice chat via PortAudio and Opus, and robust NAT traversal with STUN detection and relay fallback.

## Features

| Feature | Description |
|---------|-------------|
| Cross-platform GUI | Dear ImGui + GLFW + OpenGL 3.3 with dark theme |
| P2P Text Chat | Direct UDP messaging between peers, no server relay for data |
| Voice Chat | Real-time audio with Opus encoding and jitter buffering (optional) |
| NAT Traversal | UDP Hole Punching with STUN-based NAT type detection |
| Relay Fallback | TURN-like relay through signaling server when direct P2P fails |
| NAT Detection | Classifies NAT type (Open, Full Cone, Restricted, Symmetric) |
| Signaling Server | Lightweight peer discovery and connection brokering |
| Modern C++17 | RAII, smart pointers, `std::optional`, thread-safe queues |

## System Requirements

| Component | Requirement |
|-----------|-------------|
| Compiler | C++17 support (GCC 8+, Clang 9+, MSVC 19.14+) |
| Build Tool | CMake 3.15+ |
| GUI | GLFW 3.3+, OpenGL 3.3+ |
| Voice (optional) | PortAudio 19+, libopus 1.3+ |

Dear ImGui is vendored in the `third_party/` directory and requires no separate installation.

## Quick Start

### Ubuntu / Debian

```bash
sudo apt-get update
sudo apt-get install build-essential cmake libglfw3-dev libgl1-mesa-dev
```

To enable voice chat, also install:

```bash
sudo apt-get install libportaudio2 portaudio19-dev libopus-dev
```

### macOS (Homebrew)

```bash
brew install cmake glfw
brew install portaudio opus  # Optional, for voice
```

### Windows (vcpkg)

```powershell
vcpkg install glfw3 opengl
vcpkg install portaudio opus  # Optional, for voice
```

## Building

The project uses a standard CMake workflow:

```bash
./build.sh release          # Text chat only
./build.sh release --voice  # With voice chat support
```

Or manually:

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_VOICE=OFF
cmake --build . -j$(nproc)
```

This produces two executables in the build directory:

| Binary | Purpose |
|--------|---------|
| `p2pchat` | Client application with GUI |
| `signaling_server` | Lightweight signaling and relay server |

## Usage

### 1. Start the Signaling Server

Deploy on a machine with a public IP (or localhost for testing):

```bash
./signaling_server 9000
```

The server handles peer registration, connection brokering, and relay forwarding. It is stateless and requires minimal resources.

### 2. Launch the Client

```bash
./p2pchat
```

### 3. Connect and Chat

Open the application, then use `File -> Connect to Server...` to enter your username and the signaling server address. Once connected, the sidebar displays online peers. Right-click a peer to initiate a P2P connection. After the connection is established (indicated by green text), select the peer and begin typing in the input bar.

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                         Application                              │
├──────────┬──────────┬──────────┬──────────┬────────────────────┤
│   GUI    │  Session │ Protocol │ Network  │      Audio         │
│ (ImGui)  │ Manager  │ (Binary) │  Layer   │ (PortAudio+Opus)   │
├──────────┴──────────┴──────────┼──────────┴────────────────────┤
│                                │                                │
│  ChatWindow ──► Session ──────►│◄── UdpSocket                  │
│                    │           │◄── StunClient                  │
│                    ├── Signaling│◄── TurnClient                 │
│                    ├── P2PConn │◄── P2PConnection               │
│                    └── Audio   │                                │
└────────────────────────────────┴────────────────────────────────┘
```

| Directory | Responsibility |
|-----------|---------------|
| `src/core/` | Types, UUID generation, Session manager, thread-safe queue |
| `src/network/` | UDP socket, STUN client, TURN client, P2P connection, signaling |
| `src/protocol/` | Binary wire protocol serialization with bounds-checked reader |
| `src/ui/` | Dear ImGui-based cross-platform GUI |
| `src/audio/` | Audio engine with PortAudio I/O and Opus codec (conditional) |
| `tests/` | Integration tests with dual-client verification |

## NAT Traversal Strategy

The application employs a multi-layered connectivity strategy to maximize the probability of establishing a connection between any two peers:

1. **STUN Detection**: On startup, the client queries public STUN servers to discover its public endpoint and classify its NAT type.
2. **Direct P2P (Hole Punching)**: For Cone NAT types, simultaneous UDP packets from both peers create bidirectional NAT mappings.
3. **Relay Fallback**: When hole punching fails (e.g., Symmetric NAT on both sides), the signaling server acts as a TURN-like relay, forwarding encrypted packets between peers.

## Testing

The project includes an integration test that verifies the complete flow:

```bash
# Start the signaling server on port 9100
./signaling_server 9100 &

# Run the integration test
./tests/integration_test
```

The test creates two headless clients, registers them, establishes a P2P connection, and verifies bidirectional text message delivery.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

Dear ImGui is also licensed under the MIT License.
