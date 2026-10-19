#define PLUGIN_ASR_EXPORTS
#include "plugin/asr.h"
#include "model_file.hpp"
#include "ggml-backend.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>

// Kept for existing consumers. New clients use handle-based streaming instead.
static whisper_context* g_ctx = nullptr;
static std::string g_last_result;
static std::mutex g_mtx;
int plugin_asr_init(const char* model_path) {
    try {
        std::lock_guard lock(g_mtx);
        if (g_ctx) { whisper_free(g_ctx); g_ctx = nullptr; }
        g_last_result.clear();
        if (!model_path || !*model_path) return -1;
        auto p = whisper_context_default_params();
        // Bundled libraries are CPU-only. Let registered backends determine support.
        p.use_gpu = ggml_backend_dev_by_type(GGML_BACKEND_DEVICE_TYPE_GPU) != nullptr;
        p.flash_attn = p.use_gpu;
        g_ctx = asr::load_model(model_path, p);
        if (!g_ctx && p.flash_attn) { p.flash_attn = false; g_ctx = asr::load_model(model_path, p); }
        if (!g_ctx && p.use_gpu) { p.use_gpu = false; g_ctx = asr::load_model(model_path, p); }
        return g_ctx ? 0 : -1;
    } catch (...) { return -1; }
}
const char* plugin_asr_transcribe(const float* pcm, int count, int* out_bytes) {
    if (!out_bytes) return nullptr;
    *out_bytes = 0;
    try {
        std::lock_guard lock(g_mtx);
        if (!g_ctx || !pcm || count <= 0) return nullptr;
        for (int i = 0; i < count; ++i) if (!std::isfinite(pcm[i])) return nullptr;
        // Preserve legacy complete-utterance detection and NULL/empty semantics.
        const int tail = std::min(count, 2400);
        double square = 0;
        for (int i = count - tail; i < count; ++i) square += double(pcm[i]) * pcm[i];
        if (std::sqrt(square / tail) > .02) return nullptr;
        int voiced = 0;
        for (int i = 0; i < count - tail; ++i) if (std::abs(pcm[i]) > .01f) ++voiced;
        if (voiced < 3200) return nullptr;
        auto p = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        p.print_progress = p.print_realtime = p.print_timestamps = false;
        p.translate = false; p.language = "zh"; p.n_threads = 4;
        p.no_context = true; p.single_segment = true;
        if (whisper_full(g_ctx, p, pcm, count)) return nullptr;
        g_last_result.clear();
        for (int i = 0; i < whisper_full_n_segments(g_ctx); ++i) {
            const char* text = whisper_full_get_segment_text(g_ctx, i);
            if (text) g_last_result += text;
        }
        *out_bytes = static_cast<int>(g_last_result.size());
        return g_last_result.c_str();
    } catch (...) { return nullptr; }
}
void plugin_asr_free() {
    try {
        std::lock_guard lock(g_mtx);
        if (g_ctx) { whisper_free(g_ctx); g_ctx = nullptr; }
        g_last_result.clear();
    } catch (...) {}
}
