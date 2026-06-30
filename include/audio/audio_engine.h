#pragma once

/**
 * @file audio_engine.h
 * @brief High-level audio engine for voice chat (optional module).
 *
 * When compiled with HAS_VOICE=1, this module integrates PortAudio for
 * capture/playback and Opus for encoding/decoding. Without HAS_VOICE,
 * all methods are safe no-ops.
 */

#include "core/types.h"
#include <functional>
#include <memory>

namespace p2p {

class AudioEngine {
public:
    using SendCallback = std::function<void(uint32_t seq, uint64_t ts, std::vector<uint8_t> opus)>;

    AudioEngine();
    ~AudioEngine();

    // Non-copyable
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    /// Initialize audio subsystem. Returns false if unavailable.
    [[nodiscard]] bool init();

    /// Start capturing and encoding audio. Encoded frames are passed to `cb`.
    bool start(SendCallback cb);

    /// Stop capturing.
    void stop();

    /// Feed a received encoded audio frame for playback.
    void receive_frame(uint32_t seq, uint64_t ts, const std::vector<uint8_t>& opus);

    void set_muted(bool m) noexcept { muted_ = m; }
    [[nodiscard]] bool is_muted()  const noexcept { return muted_; }
    [[nodiscard]] bool is_active() const noexcept { return active_; }

private:
    std::atomic<bool> active_{false};
    std::atomic<bool> muted_{false};
    SendCallback      send_cb_;
    uint32_t          frame_seq_{0};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace p2p
