#pragma once

/**
 * @file audio_engine.h
 * @brief Complete audio engine for voice chat using PortAudio and Opus.
 *
 * This module provides real-time audio capture, Opus encoding/decoding,
 * jitter buffering, and playback. It is conditionally compiled:
 *   - With HAS_VOICE=1: Full PortAudio + Opus integration.
 *   - Without HAS_VOICE: Compiles but methods return failure gracefully.
 *
 * The audio pipeline:
 *   Microphone -> PortAudio Capture -> Opus Encode -> Network Send
 *   Network Recv -> Jitter Buffer -> Opus Decode -> PortAudio Playback
 */

#include "core/types.h"
#include <functional>
#include <memory>

namespace p2p {

/**
 * @class AudioEngine
 * @brief High-level audio engine managing capture, encode, decode, and playback.
 */
class AudioEngine {
public:
    /// Callback invoked when an encoded audio frame is ready for transmission.
    using SendCallback = std::function<void(uint32_t seq, uint64_t ts, std::vector<uint8_t> opus)>;

    AudioEngine();
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// Initialize the audio subsystem (PortAudio + Opus).
    /// Returns false if audio hardware is unavailable or HAS_VOICE is not defined.
    [[nodiscard]] bool init();

    /// Start capturing and encoding audio. Encoded frames are delivered via `cb`.
    bool start(SendCallback cb);

    /// Stop capturing and playback.
    void stop();

    /// Feed a received encoded audio frame for jitter-buffered playback.
    void receive_frame(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus_data);

    /// Mute/unmute the microphone (capture continues but frames are silenced).
    void set_muted(bool m) noexcept { muted_ = m; }

    [[nodiscard]] bool is_muted()  const noexcept { return muted_; }
    [[nodiscard]] bool is_active() const noexcept { return active_; }

    /// Query whether the audio subsystem was successfully initialized.
    [[nodiscard]] bool is_available() const noexcept { return available_; }

private:
    std::atomic<bool> active_{false};
    std::atomic<bool> muted_{false};
    bool              available_{false};
    SendCallback      send_cb_;
    uint32_t          frame_seq_{0};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace p2p
