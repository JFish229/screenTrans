#define PLUGIN_ASR_EXPORTS
#include "plugin/asr.h"
#include "stream_engine.hpp"
#include "whisper.h"
#include "ggml-backend.h"
#include "model_file.hpp"
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
using Clock = std::chrono::steady_clock;
double milliseconds(Clock::duration d) { return std::chrono::duration<double, std::milli>(d).count(); }
void copy_error(char* out, uint32_t cap, const char* message) {
    if (out && cap) { const size_t n = std::min<size_t>(cap - 1, std::strlen(message)); std::memcpy(out, message, n); out[n] = 0; }
}
// A token may contain only part of a Chinese UTF-8 character.
bool complete_utf8(const std::string& s) {
    int remaining = 0;
    for (unsigned char c : s) {
        if (remaining) { if ((c & 0xc0) != 0x80) return false; --remaining; }
        else if (c < 0x80) {}
        else if ((c & 0xe0) == 0xc0) remaining = 1;
        else if ((c & 0xf0) == 0xe0) remaining = 2;
        else if ((c & 0xf8) == 0xf0) remaining = 3;
        else return false;
    }
    return remaining == 0;
}
}

struct plugin_asr_stream : asr::Backend {
    struct Chunk { std::vector<float> pcm; Clock::time_point enqueued; };
    whisper_context* ctx = nullptr;
    whisper_vad_context* vad = nullptr;
    plugin_asr_config config{};
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Chunk> input;
    std::deque<asr::Event> output;
    std::thread worker;
    std::atomic<uint64_t> generation{1};
    std::atomic<bool> stopping{false};
    uint64_t working_generation = 1;
    size_t queued = 0;
    int64_t next_sample = 0, reset_position = 0;
    bool reset_pending = true, flush_pending = false, closed = false;
    std::atomic<bool> failed{false};
    double inference_ms = 0, queue_ms = 0;
    Clock::time_point batch_enqueued = Clock::now();

    ~plugin_asr_stream() {
        stopping.store(true); generation.fetch_add(1); ready.notify_all();
        if (worker.joinable()) worker.join();
        if (vad) whisper_vad_free(vad);
        if (ctx) whisper_free(ctx);
    }
    bool cancelled() const override { return stopping.load() || failed.load() || generation.load() != working_generation; }
    bool allow_partial() override {
        // A final decode can take long enough for a newer draft window to
        // accumulate while consume() is still processing its current batch.
        // Catch up with that PCM before spending another encode on stale text.
        std::lock_guard lock(mutex);
        return queued < static_cast<size_t>(config.partial_interval_ms * 16);
    }
    static bool abort_compute(void* self) { return static_cast<plugin_asr_stream*>(self)->cancelled(); }
    void reset_vad() override { whisper_vad_reset_state(vad); }
    bool speech(const float* pcm) override {
        if (!whisper_vad_detect_speech_no_reset(vad, pcm, asr::vad_frame)) throw std::runtime_error("Silero inference failed");
        const int count = whisper_vad_n_probs(vad);
        const float* probs = whisper_vad_probs(vad);
        if (count < 1 || !probs) throw std::runtime_error("Silero returned no probabilities");
        return probs[count - 1] >= 0.5f;
    }
    std::string decode(const asr::DecodeRequest& request) override {
        auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.print_progress = p.print_realtime = p.print_timestamps = p.print_special = false;
        p.language = "zh"; p.translate = false; p.detect_language = false;
        p.n_threads = config.n_threads;
        p.no_context = true; p.single_segment = true;
        p.temperature = 0; p.temperature_inc = 0; p.greedy.best_of = 1;
        p.max_tokens = request.final ? 0 : 96;
        p.suppress_blank = true; p.suppress_nst = true;
        // Only rollover windows need token timestamps; normal drafts use the fast path.
        const bool bounded = request.from > request.start || request.through < request.start + static_cast<int64_t>(request.pcm.size());
        p.token_timestamps = bounded; p.no_timestamps = !bounded;
        // Style hint is independent of the meeting/test vocabulary.
        const auto* style = reinterpret_cast<const char*>(u8"\u4ee5\u4e0b\u662f\u666e\u901a\u8bdd\u4f1a\u8bae\u8bb0\u5f55\uff0c\u4f7f\u7528\u7b80\u4f53\u4e2d\u6587\u3002");
        std::vector<whisper_token> prompt(64), history(256);
        const int prefix = whisper_tokenize(ctx, style, prompt.data(), 64);
        int nt = whisper_tokenize(ctx, request.prompt.c_str(), history.data(), static_cast<int>(history.size()));
        prompt.resize(prefix > 0 ? prefix : 0);
        if (nt > 0) {
            const int keep = std::min(nt, 64 - static_cast<int>(prompt.size()));
            prompt.insert(prompt.end(), history.begin() + nt - keep, history.begin() + nt);
        }
        p.prompt_tokens = prompt.data(); p.prompt_n_tokens = static_cast<int>(prompt.size());
        p.n_max_text_ctx = 64;
        p.abort_callback = abort_compute; p.abort_callback_user_data = this;
        // whisper.cpp short clips need padding, not altered duration/sample rate.
        std::vector<float> pcm(request.pcm);
        pcm.resize(std::max<size_t>(pcm.size(), asr::rate), 0.0f);
        const auto begin = Clock::now();
        queue_ms = milliseconds(begin - batch_enqueued);
        const int status = whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size()));
        inference_ms = milliseconds(Clock::now() - begin);
        if (cancelled()) return {};
        if (status != 0) throw std::runtime_error("Whisper inference failed");
        std::string text;
        for (int segment = 0; segment < whisper_full_n_segments(ctx); ++segment) {
            if (!bounded) { text += whisper_full_get_segment_text(ctx, segment); continue; }
            std::string bytes;
            int64_t t0 = -1, t1 = -1;
            for (int token = 0; token < whisper_full_n_tokens(ctx, segment); ++token) {
                if (whisper_full_get_token_id(ctx, segment, token) >= whisper_token_eot(ctx)) continue;
                const auto data = whisper_full_get_token_data(ctx, segment, token);
                bytes += whisper_full_get_token_text(ctx, segment, token);
                if (t0 < 0) t0 = data.t0;
                t1 = data.t1;
                if (!complete_utf8(bytes)) continue;
                if (t0 < 0 || t1 < 0) throw std::runtime_error("Missing rollover token timestamps");
                const int64_t midpoint = request.start + (t0 + t1) * 80;
                if (midpoint >= request.from && midpoint < request.through) text += bytes;
                bytes.clear(); t0 = t1 = -1;
            }
        }
        return text;
    }
    void publish(asr::Event event) {
        std::lock_guard lock(mutex);
        if (cancelled()) return;
        event.inference_ms = inference_ms; event.queue_ms = queue_ms;
        // Remove only drafts of this utterance, never other completed sentences.
        if (event.kind == asr::Event::partial || event.kind == asr::Event::final) {
            std::erase_if(output, [&](const asr::Event& e) { return e.kind == asr::Event::partial && e.id == event.id; });
        }
        if (output.size() >= 128) {
            // A stalled consumer must see an explicit discontinuity, not silent lost finals.
            output.clear(); input.clear(); queued = 0; failed = true;
            event = {}; event.kind = asr::Event::error;
            event.text = "Subtitle consumer overflow; reset required";
        }
        output.push_back(std::move(event));
    }
    void fail(const char* message) {
        // Reset may race with an exception. Check the generation and mutate
        // failed/input/output under the same lock so old failures cannot close
        // a newly reset meeting.
        std::lock_guard lock(mutex);
        if (cancelled()) return;
        asr::Event event; event.kind = asr::Event::error; event.text = message;
        failed.store(true); input.clear(); queued = 0; flush_pending = false;
        if (output.size() >= 128) output.clear();
        output.push_back(std::move(event));
    }
    void run() {
        asr::Settings s;
        s.first_ms = config.first_partial_ms; s.step_ms = config.partial_interval_ms;
        s.silence_ms = config.silence_ms; s.pre_ms = config.pre_roll_ms; s.window_ms = config.max_window_ms;
        asr::Engine engine(*this, s, [this](auto e) { publish(std::move(e)); });
        for (;;) {
            std::deque<Chunk> batch;
            bool reset = false, flush = false;
            int64_t position = 0;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] { return stopping.load() || reset_pending || !input.empty() || flush_pending; });
                if (stopping.load()) break;
                reset = reset_pending; reset_pending = false; position = reset_position;
                working_generation = generation.load();
                batch.swap(input); queued = 0;
                flush = flush_pending; flush_pending = false;
            }
            try {
                if (reset) engine.reset(position);
                // Feed one contiguous batch so outdated partial deadlines coalesce.
                std::vector<float> pcm;
                for (auto& chunk : batch) pcm.insert(pcm.end(), chunk.pcm.begin(), chunk.pcm.end());
                batch_enqueued = batch.empty() ? Clock::now() : batch.front().enqueued;
                if (!pcm.empty()) engine.consume(pcm.data(), pcm.size());
                if (flush && !cancelled()) {
                    engine.finish(); asr::Event event; event.kind = asr::Event::drained; publish(event);
                }
            } catch (const std::exception& e) {
                fail(e.what());
            } catch (...) {
                fail("Unknown ASR worker failure");
            }
        }
    }
};

void plugin_asr_stream_default_config(plugin_asr_config* c) {
    if (!c) return;
    *c = {}; c->struct_size = sizeof(*c); c->abi_version = PLUGIN_ASR_ABI_VERSION;
    c->model_path = "plugin/ggml-base.bin"; c->vad_model_path = "plugin/ggml-silero-v6.2.0.bin";
    c->n_threads = std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8); c->use_gpu = 1;
    c->first_partial_ms = 512; c->partial_interval_ms = 400; c->silence_ms = 300;
    c->pre_roll_ms = 200; c->max_window_ms = 8000; c->queue_ms = 2000;
}
int plugin_asr_stream_create(const plugin_asr_config* c, plugin_asr_stream** out, char* error, uint32_t cap) {
    if (out) *out = nullptr;
    copy_error(error, cap, "");
    if (!c || !out || c->struct_size != sizeof(*c) || c->abi_version != PLUGIN_ASR_ABI_VERSION ||
        !c->model_path || !c->vad_model_path || c->n_threads < 1 || c->n_threads > 64 ||
        c->first_partial_ms < 160 || c->first_partial_ms > 2000 ||
        c->partial_interval_ms < 100 || c->partial_interval_ms > 2000 ||
        c->silence_ms < 64 || c->silence_ms > 2000 || c->pre_roll_ms < 0 || c->pre_roll_ms > 1000 ||
        c->max_window_ms < 3000 || c->max_window_ms > 15000 || c->queue_ms < 100 || c->queue_ms > 10000) {
        copy_error(error, cap, "Invalid ASR configuration/ABI"); return PLUGIN_ASR_INVALID;
    }
    try {
        auto stream = std::make_unique<plugin_asr_stream>(); stream->config = *c;
        // Registration/loading is process-global; serialize concurrent creates.
        static std::mutex loading;
        std::lock_guard guard(loading);
        whisper_log_set([](ggml_log_level level, const char* message, void*) {
            // Upstream logs four INFO lines per VAD frame; keep warnings/errors.
            if (level <= GGML_LOG_LEVEL_INFO && std::strstr(message, "whisper_vad_detect_speech_no_reset:")) return;
            std::fputs(message, stderr);
        }, nullptr);
        ggml_backend_load_all();
        bool gpu_available = false;
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            auto dev = ggml_backend_dev_get(i);
            std::fprintf(stderr, "ASR backend: %s (%s)\n", ggml_backend_dev_name(dev), ggml_backend_dev_description(dev));
            gpu_available |= ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_GPU;
        }
        auto params = whisper_context_default_params();
        params.use_gpu = c->use_gpu && gpu_available;
        params.flash_attn = params.use_gpu;
        if (c->use_gpu && !gpu_available) std::fprintf(stderr, "ASR: no GPU backend registered; CPU fallback\n");
        stream->ctx = asr::load_model(c->model_path, params);
        if (!stream->ctx && params.flash_attn) {
            params.flash_attn = false; stream->ctx = asr::load_model(c->model_path, params);
        }
        if (!stream->ctx && params.use_gpu) {
            std::fprintf(stderr, "ASR: GPU initialization failed; CPU fallback\n");
            params.use_gpu = false; stream->ctx = asr::load_model(c->model_path, params);
        }
        if (!stream->ctx) throw std::runtime_error("Cannot load ASR model; check model path/backend logs");
        if (!whisper_is_multilingual(stream->ctx)) throw std::runtime_error("Chinese ASR requires a multilingual model (not .en)");
        auto vp = whisper_vad_default_context_params(); vp.use_gpu = false; vp.n_threads = 1;
        stream->vad = asr::load_vad(c->vad_model_path, vp);
        if (!stream->vad) throw std::runtime_error("Cannot load Silero VAD model");
        std::vector<float> silence(asr::rate, 0.0f);
        stream->decode({silence, {}, false, 0, 0, asr::rate}); // startup warmup excluded from live metrics
        stream->config.model_path = stream->config.vad_model_path = nullptr;
        stream->worker = std::thread([self = stream.get()] { self->run(); });
        *out = stream.release(); return PLUGIN_ASR_OK;
    } catch (const std::exception& e) { copy_error(error, cap, e.what()); }
    catch (...) { copy_error(error, cap, "Unknown ASR initialization failure"); }
    return PLUGIN_ASR_FAILED;
}
int plugin_asr_stream_push(plugin_asr_stream* s, const float* pcm, uint32_t count) {
    if (!s || (!pcm && count) || count > asr::rate * 60) return PLUGIN_ASR_INVALID;
    for (uint32_t i = 0; i < count; ++i) if (!std::isfinite(pcm[i]) || std::abs(pcm[i]) > 1.001f) return PLUGIN_ASR_INVALID;
    try {
        std::lock_guard lock(s->mutex);
        if (s->closed || s->failed || s->stopping.load()) return PLUGIN_ASR_CLOSED;
        if (!count) return PLUGIN_ASR_OK;
        const size_t capacity = s->config.queue_ms * 16;
        int result = PLUGIN_ASR_OK;
        const size_t keep = std::min<size_t>(count, capacity);
        if (s->queued + count > capacity) {
            s->generation.fetch_add(1); s->reset_pending = true;
            s->reset_position = s->next_sample + count - keep;
            s->input.clear(); s->queued = 0;
            std::erase_if(s->output, [](const auto& e) { return e.kind == asr::Event::partial; });
            asr::Event event; event.kind = asr::Event::gap; event.start = s->reset_position;
            event.text = "ASR audio queue overflow; stale audio cancelled";
            // Keep this queue bounded even if the caller never polls.
            if (s->output.size() >= 128) s->output.clear();
            s->output.push_back(std::move(event)); result = PLUGIN_ASR_OVERFLOW;
        }
        s->input.push_back({std::vector<float>(pcm + count - keep, pcm + count), Clock::now()});
        s->queued += keep; s->next_sample += count;
        s->ready.notify_one(); return result;
    } catch (...) { return PLUGIN_ASR_FAILED; }
}
int plugin_asr_stream_poll(plugin_asr_stream* s, plugin_asr_result* out, char* text, uint32_t capacity) {
    if (!s || !out || out->struct_size != sizeof(*out)) return PLUGIN_ASR_INVALID;
    try {
        std::lock_guard lock(s->mutex);
        if (s->output.empty()) return PLUGIN_ASR_OK;
        const auto& e = s->output.front();
        *out = {}; out->struct_size = sizeof(*out); out->type = e.kind;
        out->utterance_id = e.id; out->revision = e.revision;
        out->start_sample = e.start; out->end_sample = e.end;
        out->inference_ms = e.inference_ms; out->queue_ms = e.queue_ms;
        out->text_bytes = static_cast<uint32_t>(e.text.size() + 1);
        if (!text || capacity < out->text_bytes) return PLUGIN_ASR_BUFFER_SMALL;
        std::memcpy(text, e.text.c_str(), out->text_bytes); s->output.pop_front(); return PLUGIN_ASR_EVENT;
    } catch (...) { return PLUGIN_ASR_FAILED; }
}
int plugin_asr_stream_flush(plugin_asr_stream* s) {
    if (!s) return PLUGIN_ASR_INVALID;
    try {
        std::lock_guard lock(s->mutex);
        if (s->failed) return PLUGIN_ASR_CLOSED;
        if (!s->closed) { s->closed = true; s->flush_pending = true; s->ready.notify_one(); }
        return PLUGIN_ASR_OK;
    } catch (...) { return PLUGIN_ASR_FAILED; }
}
int plugin_asr_stream_reset(plugin_asr_stream* s) {
    if (!s) return PLUGIN_ASR_INVALID;
    try {
        std::lock_guard lock(s->mutex);
        s->generation.fetch_add(1); s->reset_pending = true; s->reset_position = s->next_sample = 0;
        s->input.clear(); s->output.clear(); s->queued = 0;
        s->flush_pending = s->closed = false; s->failed.store(false); s->ready.notify_one(); return PLUGIN_ASR_OK;
    } catch (...) { return PLUGIN_ASR_FAILED; }
}
void plugin_asr_stream_destroy(plugin_asr_stream* stream) { try { delete stream; } catch (...) {} }
