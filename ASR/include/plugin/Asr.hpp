#pragma once
#include <plugin/asr.h>
namespace plugin {
namespace fn {
using AsrInit = decltype(&plugin_asr_init);
using AsrTranscribe = decltype(&plugin_asr_transcribe);
using AsrFree = decltype(&plugin_asr_free);
using AsrDefaults = decltype(&plugin_asr_stream_default_config);
using AsrCreate = decltype(&plugin_asr_stream_create);
using AsrPush = decltype(&plugin_asr_stream_push);
using AsrPoll = decltype(&plugin_asr_stream_poll);
using AsrFlush = decltype(&plugin_asr_stream_flush);
using AsrReset = decltype(&plugin_asr_stream_reset);
using AsrDestroy = decltype(&plugin_asr_stream_destroy);
}
namespace name {
constexpr const char* asrInit = "plugin_asr_init";
constexpr const char* asrTranscribe = "plugin_asr_transcribe";
constexpr const char* asrFree = "plugin_asr_free";
constexpr const char* asrDefaults = "plugin_asr_stream_default_config";
constexpr const char* asrCreate = "plugin_asr_stream_create";
constexpr const char* asrPush = "plugin_asr_stream_push";
constexpr const char* asrPoll = "plugin_asr_stream_poll";
constexpr const char* asrFlush = "plugin_asr_stream_flush";
constexpr const char* asrReset = "plugin_asr_stream_reset";
constexpr const char* asrDestroy = "plugin_asr_stream_destroy";
}
}
