#pragma once
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace asr_audio {
class CaptureGate {
public:
    bool filter(std::vector<float>& pcm, bool enabled) {
        const bool changed = enabled != enabled_;
        // The block straddling a mute/unmute boundary has unknown ownership.
        if (!enabled || changed) pcm.clear();
        enabled_ = enabled;
        return changed;
    }
private:
    bool enabled_ = false;
};
inline void add(std::span<float> destination, std::span<const float> source) {
    const auto n = std::min(destination.size(), source.size());
    for (size_t i = 0; i < n; ++i) destination[i] += source[i];
}
inline void add_remote(std::span<float> playback, std::span<const float> received, bool self_echo) {
    if (!self_echo) add(playback, received);
}
inline void normalize(std::span<float> pcm) {
    float peak = 1.0f;
    for (float sample : pcm) peak = std::max(peak, std::abs(sample));
    for (float& sample : pcm) sample /= peak;
}
}
