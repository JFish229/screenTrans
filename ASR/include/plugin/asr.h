#pragma once
#include <stdint.h>
#if defined(_WIN32)
# if defined(PLUGIN_ASR_EXPORTS)
#  define PLUGIN_ASR_API __declspec(dllexport)
# else
#  define PLUGIN_ASR_API __declspec(dllimport)
# endif
#else
# define PLUGIN_ASR_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
// Legacy ABI: full 16 kHz mono float buffers, UTF-8 owned by the DLL.
// NULL = incomplete/error; "" = completed empty result. Never free the text.
PLUGIN_ASR_API int plugin_asr_init(const char* model_path);
PLUGIN_ASR_API const char* plugin_asr_transcribe(const float* pcm_data, int n_samples, int* out_bytes);
PLUGIN_ASR_API void plugin_asr_free(void);
#define PLUGIN_ASR_ABI_VERSION 1
enum plugin_asr_status {
    PLUGIN_ASR_OK = 0, PLUGIN_ASR_EVENT = 1, PLUGIN_ASR_OVERFLOW = 2,
    PLUGIN_ASR_INVALID = -1, PLUGIN_ASR_BUFFER_SMALL = -2,
    PLUGIN_ASR_FAILED = -3, PLUGIN_ASR_CLOSED = -4
};
enum plugin_asr_event_type {
    PLUGIN_ASR_PARTIAL = 1, PLUGIN_ASR_FINAL = 2, PLUGIN_ASR_GAP = 3,
    PLUGIN_ASR_ERROR = 4, PLUGIN_ASR_DRAINED = 5
};
typedef struct plugin_asr_stream plugin_asr_stream;
typedef struct plugin_asr_config {
    uint32_t struct_size, abi_version;
    const char* model_path;
    const char* vad_model_path;
    int32_t n_threads, use_gpu;
    int32_t first_partial_ms, partial_interval_ms, silence_ms;
    int32_t pre_roll_ms, max_window_ms, queue_ms;
} plugin_asr_config;
typedef struct plugin_asr_result {
    uint32_t struct_size, type;
    uint64_t utterance_id, revision;
    int64_t start_sample, end_sample;
    uint32_t text_bytes; // includes NUL; filled even on BUFFER_SMALL.
    uint32_t reserved;
    double inference_ms, queue_ms;
} plugin_asr_result;
PLUGIN_ASR_API void plugin_asr_stream_default_config(plugin_asr_config* config);
// Synchronous load/warmup. Config strings borrowed only during create.
PLUGIN_ASR_API int plugin_asr_stream_create(const plugin_asr_config* config,
    plugin_asr_stream** stream, char* error, uint32_t error_capacity);
// Copies finite normalized 16 kHz mono PCM; never runs inference on the caller.
// OVERFLOW accepts newest audio, cancels stale work and emits GAP.
PLUGIN_ASR_API int plugin_asr_stream_push(plugin_asr_stream* stream, const float* pcm, uint32_t samples);
// Single consumer. BUFFER_SMALL preserves the event; retry with text_bytes.
PLUGIN_ASR_API int plugin_asr_stream_poll(plugin_asr_stream* stream,
    plugin_asr_result* result, char* text, uint32_t text_capacity);
// Async drain; pushes rejected until reset. Emits final then DRAINED.
PLUGIN_ASR_API int plugin_asr_stream_flush(plugin_asr_stream* stream);
// Cancel old generation, clear audio/results; next push starts at sample 0.
PLUGIN_ASR_API int plugin_asr_stream_reset(plugin_asr_stream* stream);
// Stop other API callers before destroy, which cancels and joins the worker.
PLUGIN_ASR_API void plugin_asr_stream_destroy(plugin_asr_stream* stream);
#ifdef __cplusplus
}
#endif
#undef PLUGIN_ASR_API
