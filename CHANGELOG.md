# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [2.0.0] - 2026-06-29

### Added
- Cross-platform GUI using Dear ImGui + GLFW + OpenGL 3.3.
- Dark theme with modern styling.
- Sidebar peer list with connection status indicators.
- Right-click context menu for peer actions.
- Connection dialog with server address input.
- Status bar showing local/public endpoint information.
- Audio engine module (stub, requires PortAudio + Opus for full functionality).
- `.clang-format` for consistent code formatting.
- `CONTRIBUTING.md` with coding standards.
- `CHANGELOG.md` for version tracking.

### Changed
- Complete architecture rewrite using modern C++17 idioms.
- Protocol parser now includes full bounds checking via `ByteReader::has()`.
- `UdpSocket::bind()` guards against double-bind.
- `P2PConnection` heartbeat uses mutex to prevent duplicate thread spawning.
- Move constructor for `UdpSocket` now correctly transfers handler.
- Signaling server performs stale peer cleanup every 5 seconds.

### Removed
- Terminal-based UI (replaced by GUI).
- Redundant `event_loop.cpp` and `peer_info.cpp`.
- Empty placeholder audio files.
- Unused design documents from source tree.

## [1.0.0] - 2026-06-28

### Added
- Initial implementation with terminal UI.
- UDP-based P2P text messaging.
- NAT hole-punching via signaling server.
- Cross-platform build system (CMake).
