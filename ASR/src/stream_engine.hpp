#pragma once
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

namespace asr {
constexpr int rate = 16000;
constexpr int vad_frame = 512; // Silero's 32 ms window at 16 kHz.
struct Settings {
    int first_ms = 512, step_ms = 400, silence_ms = 300;
    int pre_ms = 200, window_ms = 8000, min_ms = 160;
};
struct DecodeRequest {
    const std::vector<float>& pcm;
    const std::string& prompt;
    bool final;
    int64_t start, from, through; // absolute sample positions, [from, through).
};
struct Event {
    enum Kind { partial = 1, final = 2, gap = 3, error = 4, drained = 5 };
    int kind = partial;
    uint64_t id = 0, revision = 0;
    int64_t start = 0, end = 0;
    double inference_ms = 0, queue_ms = 0;
    std::string text;
};
struct Backend {
    virtual ~Backend() = default;
    virtual bool speech(const float* frame) = 0;
    virtual std::string decode(const DecodeRequest&) = 0;
    virtual void reset_vad() = 0;
    virtual bool cancelled() const = 0;
    virtual bool allow_partial() { return true; }
};
// All segmentation state belongs to the worker. Producers never mutate it.
class Engine {
public:
    Engine(Backend& backend, Settings settings, std::function<void(Event)> emit)
        : backend_(backend), settings_(settings), emit_(std::move(emit)) {}
    void reset(int64_t position = 0) {
        active_ = false;
        frame_.clear(); audio_.clear(); pre_.clear(); prompt_.clear();
        cursor_ = position; voiced_ = 0; revision_ = 0;
        backend_.reset_vad();
    }
    void consume(const float* data, size_t count) {
        while (count && !backend_.cancelled()) {
            const size_t n = std::min(count, size_t(vad_frame) - frame_.size());
            frame_.insert(frame_.end(), data, data + n);
            data += n; count -= n;
            if (frame_.size() == vad_frame) { process_frame(); frame_.clear(); }
        }
        // Coalesce obsolete draft deadlines in a received batch.
        if (active_ && voiced_ >= settings_.first_ms * 16 && cursor_ >= next_draft_ && !backend_.cancelled() && backend_.allow_partial()) {
            recognize(false, cursor_);
            next_draft_ = cursor_ + settings_.step_ms * 16;
        }
    }
    void finish() {
        const int64_t actual_end = cursor_ + static_cast<int64_t>(frame_.size());
        if (!frame_.empty()) {
            frame_.resize(vad_frame, 0.0f);
            process_frame(); frame_.clear();
        }
        cursor_ = actual_end;
        if (active_ && !backend_.cancelled()) end_utterance(cursor_);
    }
private:
    void process_frame() {
        const bool speech = backend_.speech(frame_.data());
        if (!active_ && speech) {
            active_ = true; ++id_; revision_ = 0; voiced_ = 0;
            audio_.assign(pre_.begin(), pre_.end());
            start_ = cursor_ - static_cast<int64_t>(audio_.size()); from_ = start_;
            next_draft_ = cursor_ + settings_.first_ms * 16;
        }
        if (active_) {
            audio_.insert(audio_.end(), frame_.begin(), frame_.end());
            if (speech) { last_voice_ = cursor_ + vad_frame; voiced_ += vad_frame; }
        }
        cursor_ += vad_frame;
        pre_.insert(pre_.end(), frame_.begin(), frame_.end());
        while (pre_.size() > static_cast<size_t>(settings_.pre_ms * 16)) pre_.pop_front();
        if (!active_ || backend_.cancelled()) return;
        if (cursor_ - last_voice_ >= settings_.silence_ms * 16) {
            end_utterance(cursor_);
        } else if (audio_.size() >= static_cast<size_t>(settings_.window_ms * 16)) {
            // Defer the last 320 ms; re-decode it with left context next time.
            const int64_t boundary = cursor_ - 320 * 16;
            recognize(true, boundary);
            const int64_t next_start = boundary - settings_.pre_ms * 16;
            audio_.erase(audio_.begin(), audio_.begin() + (next_start - start_));
            start_ = next_start; from_ = boundary; ++id_; revision_ = 0;
            next_draft_ = cursor_ + settings_.step_ms * 16;
        }
    }
    void end_utterance(int64_t end) {
        if (voiced_ >= settings_.min_ms * 16) recognize(true, end);
        active_ = false; audio_.clear(); voiced_ = 0;
        // Preserve VAD state across pauses; reset only on a stream break.
    }
    void recognize(bool final, int64_t through) {
        const auto text = backend_.decode({audio_, prompt_, final, start_, from_, through});
        if (backend_.cancelled()) return;
        Event event;
        event.kind = final ? Event::final : Event::partial;
        event.id = id_; event.revision = ++revision_;
        event.start = from_; event.end = through; event.text = text;
        emit_(std::move(event)); // Empty final removes an earlier draft too.
        if (final && !text.empty()) {
            prompt_ += text;
            if (prompt_.size() > 512) {
                size_t cut = prompt_.size() - 512;
                while (cut < prompt_.size() && (static_cast<unsigned char>(prompt_[cut]) & 0xc0) == 0x80) ++cut;
                prompt_.erase(0, cut);
            }
        }
    }
    Backend& backend_;
    Settings settings_;
    std::function<void(Event)> emit_;
    std::vector<float> frame_, audio_;
    std::deque<float> pre_;
    std::string prompt_;
    bool active_ = false;
    uint64_t id_ = 0, revision_ = 0;
    int64_t cursor_ = 0, start_ = 0, from_ = 0, last_voice_ = 0, voiced_ = 0, next_draft_ = 0;
};
}
