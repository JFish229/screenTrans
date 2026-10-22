#pragma once
#include <algorithm>
#include <cmath>
#include <span>
#include <vector>

namespace asr_audio {
// A delivery burst is not a discontinuity. Only an actual capacity overrun
// loses PCM; retain ordinary device/network jitter until the mixer consumes it.
template<class Queue>
size_t pop_audio(Queue& queue, std::span<float> out) {
    const auto n = std::min(queue.size(), out.size());
    std::copy_n(queue.begin(), n, out.begin());
    queue.erase_begin(n);
    return n;
}
template<class Queue>
size_t append_audio(Queue& queue, std::span<const float> pcm) {
    const auto dropped = pcm.size() > queue.capacity() - queue.size()
        ? pcm.size() - (queue.capacity() - queue.size()) : 0;
    queue.insert(queue.end(), pcm.begin(), pcm.end());
    return dropped;
}
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
inline float peak(std::span<const float> pcm) {
    float value = 0;
    for (float sample : pcm) value = std::max(value, std::abs(sample));
    return value;
}
}
