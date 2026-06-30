#include "audio/audio_engine.h"

namespace p2p {

struct AudioEngine::Impl {
    // When HAS_VOICE is defined, this struct holds:
    //   - PortAudio stream handles
    //   - Opus encoder/decoder state
    //   - Jitter buffer for playback
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::init() {
#ifdef HAS_VOICE
    // TODO: Initialize PortAudio + Opus
    return true;
#else
    return false;
#endif
}

bool AudioEngine::start(SendCallback cb) {
    if (active_) return true;
    send_cb_ = std::move(cb);
    active_ = true;
    return true;
}

void AudioEngine::stop() {
    active_ = false;
}

void AudioEngine::receive_frame(uint32_t /*seq*/, uint64_t /*ts*/,
                                const std::vector<uint8_t>& /*opus*/) {
    // When HAS_VOICE: decode with Opus, buffer in jitter buffer, play via PortAudio
}

} // namespace p2p
