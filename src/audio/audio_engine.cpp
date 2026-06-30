#include "audio/audio_engine.h"

#ifdef HAS_VOICE
  #include <portaudio.h>
  #include <opus/opus.h>
#endif

#include <algorithm>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace p2p {

// ─── Implementation Detail (PIMPL) ──────────────────────────────────────────

struct AudioEngine::Impl {
#ifdef HAS_VOICE
    // PortAudio streams
    PaStream*      capture_stream{nullptr};
    PaStream*      playback_stream{nullptr};

    // Opus codec
    OpusEncoder*   encoder{nullptr};
    OpusDecoder*   decoder{nullptr};

    // Jitter buffer: stores decoded PCM frames ordered by sequence number
    struct JitterEntry {
        uint32_t seq;
        std::vector<float> pcm;
    };
    std::mutex              jitter_mtx;
    std::vector<JitterEntry> jitter_buf;
    uint32_t                playback_seq{0};

    // Capture buffer for accumulating samples
    std::vector<float>      capture_buf;
    std::mutex              capture_mtx;

    // Capture callback (called by PortAudio in real-time thread)
    static int capture_callback(const void* input, void* /*output*/,
                                unsigned long frame_count,
                                const PaStreamCallbackTimeInfo* /*time_info*/,
                                PaStreamCallbackFlags /*flags*/,
                                void* user_data) {
        auto* impl = static_cast<Impl*>(user_data);
        if (!input) return paContinue;

        const float* in = static_cast<const float*>(input);
        std::lock_guard<std::mutex> lock(impl->capture_mtx);
        impl->capture_buf.insert(impl->capture_buf.end(), in, in + frame_count);
        return paContinue;
    }

    // Playback callback (called by PortAudio in real-time thread)
    static int playback_callback(const void* /*input*/, void* output,
                                 unsigned long frame_count,
                                 const PaStreamCallbackTimeInfo* /*time_info*/,
                                 PaStreamCallbackFlags /*flags*/,
                                 void* user_data) {
        auto* impl = static_cast<Impl*>(user_data);
        float* out = static_cast<float*>(output);

        std::lock_guard<std::mutex> lock(impl->jitter_mtx);

        // Find the next frame in sequence
        size_t samples_written = 0;
        while (samples_written < frame_count && !impl->jitter_buf.empty()) {
            // Find lowest seq >= playback_seq
            auto it = std::min_element(impl->jitter_buf.begin(), impl->jitter_buf.end(),
                [](const JitterEntry& a, const JitterEntry& b) { return a.seq < b.seq; });

            if (it != impl->jitter_buf.end()) {
                size_t to_copy = std::min(it->pcm.size(),
                                          static_cast<size_t>(frame_count - samples_written));
                std::memcpy(out + samples_written, it->pcm.data(), to_copy * sizeof(float));
                samples_written += to_copy;
                impl->playback_seq = it->seq + 1;
                impl->jitter_buf.erase(it);
            } else {
                break;
            }
        }

        // Fill remaining with silence
        if (samples_written < frame_count) {
            std::memset(out + samples_written, 0,
                        (frame_count - samples_written) * sizeof(float));
        }

        return paContinue;
    }
#endif // HAS_VOICE
};

// ─── AudioEngine Lifecycle ───────────────────────────────────────────────────

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}

AudioEngine::~AudioEngine() { stop(); }

bool AudioEngine::init() {
#ifdef HAS_VOICE
    // Initialize PortAudio
    PaError err = Pa_Initialize();
    if (err != paNoError) return false;

    // Create Opus encoder (48kHz, mono, VOIP application)
    int opus_err;
    impl_->encoder = opus_encoder_create(
        audio_config::kSampleRate, audio_config::kChannels,
        OPUS_APPLICATION_VOIP, &opus_err);
    if (opus_err != OPUS_OK || !impl_->encoder) {
        Pa_Terminate();
        return false;
    }
    opus_encoder_ctl(impl_->encoder, OPUS_SET_BITRATE(audio_config::kBitrate));
    opus_encoder_ctl(impl_->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
    opus_encoder_ctl(impl_->encoder, OPUS_SET_COMPLEXITY(5));

    // Create Opus decoder
    impl_->decoder = opus_decoder_create(
        audio_config::kSampleRate, audio_config::kChannels, &opus_err);
    if (opus_err != OPUS_OK || !impl_->decoder) {
        opus_encoder_destroy(impl_->encoder);
        impl_->encoder = nullptr;
        Pa_Terminate();
        return false;
    }

    available_ = true;
    return true;
#else
    available_ = false;
    return false;
#endif
}

bool AudioEngine::start(SendCallback cb) {
#ifdef HAS_VOICE
    if (!available_ || active_) return active_;

    send_cb_ = std::move(cb);
    frame_seq_ = 0;
    impl_->capture_buf.clear();
    impl_->jitter_buf.clear();
    impl_->playback_seq = 0;

    // Open capture stream
    PaError err = Pa_OpenDefaultStream(
        &impl_->capture_stream,
        audio_config::kChannels,  // input channels
        0,                         // output channels (capture only)
        paFloat32,
        audio_config::kSampleRate,
        audio_config::kFrameSize,
        &Impl::capture_callback,
        impl_.get());
    if (err != paNoError) return false;

    // Open playback stream
    err = Pa_OpenDefaultStream(
        &impl_->playback_stream,
        0,                         // input channels (playback only)
        audio_config::kChannels,  // output channels
        paFloat32,
        audio_config::kSampleRate,
        audio_config::kFrameSize,
        &Impl::playback_callback,
        impl_.get());
    if (err != paNoError) {
        Pa_CloseStream(impl_->capture_stream);
        impl_->capture_stream = nullptr;
        return false;
    }

    Pa_StartStream(impl_->capture_stream);
    Pa_StartStream(impl_->playback_stream);
    active_ = true;

    // Start encoder thread: pulls from capture buffer, encodes, sends
    std::thread([this] {
        std::vector<float> frame(audio_config::kFrameSize);
        std::vector<uint8_t> opus_buf(4000); // Max Opus frame size

        while (active_) {
            bool have_frame = false;
            {
                std::lock_guard<std::mutex> lock(impl_->capture_mtx);
                if (impl_->capture_buf.size() >= static_cast<size_t>(audio_config::kFrameSize)) {
                    std::copy_n(impl_->capture_buf.begin(), audio_config::kFrameSize, frame.begin());
                    impl_->capture_buf.erase(
                        impl_->capture_buf.begin(),
                        impl_->capture_buf.begin() + audio_config::kFrameSize);
                    have_frame = true;
                }
            }

            if (have_frame && send_cb_) {
                if (muted_) {
                    // Send silence frame (comfort noise)
                    std::fill(frame.begin(), frame.end(), 0.0f);
                }

                int encoded_bytes = opus_encode_float(
                    impl_->encoder, frame.data(), audio_config::kFrameSize,
                    opus_buf.data(), static_cast<int>(opus_buf.size()));

                if (encoded_bytes > 0) {
                    std::vector<uint8_t> opus_frame(opus_buf.begin(),
                                                    opus_buf.begin() + encoded_bytes);
                    send_cb_(++frame_seq_, now_ms(), std::move(opus_frame));
                }
            } else {
                std::this_thread::sleep_for(Millis(5));
            }
        }
    }).detach();

    return true;
#else
    (void)cb;
    return false;
#endif
}

void AudioEngine::stop() {
#ifdef HAS_VOICE
    if (!active_) return;
    active_ = false;

    // Allow encoder thread to exit
    std::this_thread::sleep_for(Millis(50));

    if (impl_->capture_stream) {
        Pa_StopStream(impl_->capture_stream);
        Pa_CloseStream(impl_->capture_stream);
        impl_->capture_stream = nullptr;
    }
    if (impl_->playback_stream) {
        Pa_StopStream(impl_->playback_stream);
        Pa_CloseStream(impl_->playback_stream);
        impl_->playback_stream = nullptr;
    }
    if (impl_->encoder) {
        opus_encoder_destroy(impl_->encoder);
        impl_->encoder = nullptr;
    }
    if (impl_->decoder) {
        opus_decoder_destroy(impl_->decoder);
        impl_->decoder = nullptr;
    }
    Pa_Terminate();
    available_ = false;
#else
    active_ = false;
#endif
}

void AudioEngine::receive_frame(uint32_t seq, uint64_t /*ts*/,
                                const std::vector<uint8_t>& opus_data) {
#ifdef HAS_VOICE
    if (!available_ || !impl_->decoder) return;

    // Decode Opus frame to PCM
    std::vector<float> pcm(audio_config::kFrameSize);
    int decoded_samples = opus_decode_float(
        impl_->decoder, opus_data.data(), static_cast<int>(opus_data.size()),
        pcm.data(), audio_config::kFrameSize, 0);

    if (decoded_samples <= 0) return;
    pcm.resize(static_cast<size_t>(decoded_samples));

    // Insert into jitter buffer
    std::lock_guard<std::mutex> lock(impl_->jitter_mtx);

    // Drop if buffer is too full (prevents unbounded growth)
    if (impl_->jitter_buf.size() >= static_cast<size_t>(audio_config::kJitterBufFrames)) {
        // Remove oldest entry
        auto oldest = std::min_element(impl_->jitter_buf.begin(), impl_->jitter_buf.end(),
            [](const Impl::JitterEntry& a, const Impl::JitterEntry& b) {
                return a.seq < b.seq;
            });
        if (oldest != impl_->jitter_buf.end()) {
            impl_->jitter_buf.erase(oldest);
        }
    }

    impl_->jitter_buf.push_back({seq, std::move(pcm)});
#else
    (void)seq;
    (void)opus_data;
#endif
}

} // namespace p2p
