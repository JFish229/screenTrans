// Real deployed-DLL boundary test of the same bounded PCM adapter as App.cpp.
// Run two processes, one local and one remote, to exercise concurrent CPU ASR.
#include "plugin/asr.h"
#include "../../ScreenTrans_Client/include/AsrAudio.hpp"
#include <boost/circular_buffer.hpp>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

static void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
static std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
static std::vector<float> read_wav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    char header[12]{}; file.read(header, sizeof(header));
    require(file && !std::memcmp(header, "RIFF", 4) && !std::memcmp(header + 8, "WAVE", 4), "invalid WAV");
    bool format = false;
    for (;;) {
        char kind[4]{}; uint32_t size = 0;
        file.read(kind, 4); file.read(reinterpret_cast<char*>(&size), 4);
        require(bool(file), "missing WAV data");
        if (!std::memcmp(kind, "fmt ", 4)) {
            require(size >= 16 && size <= 4096, "invalid WAV format chunk");
            std::vector<char> bytes(size); file.read(bytes.data(), size);
            uint16_t encoding, channels, bits; uint32_t rate;
            std::memcpy(&encoding, bytes.data(), 2); std::memcpy(&channels, bytes.data() + 2, 2);
            std::memcpy(&rate, bytes.data() + 4, 4); std::memcpy(&bits, bytes.data() + 14, 2);
            require(encoding == 1 && channels == 1 && rate == 16000 && bits == 16, "expected PCM16 mono 16kHz WAV");
            format = true;
        } else if (!std::memcmp(kind, "data", 4)) {
            require(format && size > 0 && size % 2 == 0 && size <= 16000 * 2 * 60, "invalid WAV data size");
            std::vector<int16_t> raw(size / 2); file.read(reinterpret_cast<char*>(raw.data()), size);
            require(bool(file), "truncated WAV");
            std::vector<float> result; result.reserve(raw.size());
            for (auto sample : raw) result.push_back(sample / 32768.f);
            return result;
        } else { file.seekg(size, std::ios::cur); }
        if (size & 1) file.seekg(1, std::ios::cur);
    }
}
int wmain(int argc, wchar_t** argv) try {
    require(argc == 5 || argc == 6, "usage: asr_pipeline_test <model> <vad> <PCM16-WAV> <local|remote> [threads]");
    const bool local = std::wstring(argv[4]) == L"local";
    require(local || std::wstring(argv[4]) == L"remote", "route must be local or remote");
    const auto audio = read_wav(argv[3]);
    const auto model = utf8(argv[1]), vad = utf8(argv[2]);
    plugin_asr_config config{}; plugin_asr_stream_default_config(&config);
    config.model_path = model.c_str(); config.vad_model_path = vad.c_str();
    config.n_threads = argc == 6 ? std::stoi(argv[5]) : 4; config.use_gpu = 0;
    plugin_asr_stream* stream = nullptr; char error[2048]{};
    const auto created = plugin_asr_stream_create(&config, &stream, error, sizeof(error));
    if (created != PLUGIN_ASR_OK) throw std::runtime_error(error);
    struct Cleanup { plugin_asr_stream* stream; ~Cleanup() { plugin_asr_stream_destroy(stream); } } cleanup{stream};
    boost::circular_buffer<float> queue(16000);
    size_t source = 0, consumed = 0, ticks = 0, finals = 0, nonempty = 0;
    bool drained = false;
    auto poll = [&] {
        for (;;) {
            plugin_asr_result result{}; result.struct_size = sizeof(result);
            std::vector<char> text(4096);
            auto status = plugin_asr_stream_poll(stream, &result, text.data(), static_cast<uint32_t>(text.size()));
            if (status == PLUGIN_ASR_BUFFER_SMALL) {
                text.resize(result.text_bytes);
                status = plugin_asr_stream_poll(stream, &result, text.data(), static_cast<uint32_t>(text.size()));
            }
            require(status >= 0, "poll failed");
            if (status != PLUGIN_ASR_EVENT) break;
            require(result.type != PLUGIN_ASR_GAP && result.type != PLUGIN_ASR_ERROR, "unexpected GAP/ERROR");
            if (result.type == PLUGIN_ASR_FINAL) ++finals;
            if (text[0]) { ++nonempty; std::cout << "event=" << result.type << " inference_ms=" << result.inference_ms << " queue_ms=" << result.queue_ms << " text=" << text.data() << '\n'; }
            drained |= result.type == PLUGIN_ASR_DRAINED;
        }
    };
    const auto start = std::chrono::steady_clock::now();
    // First packet arrives after 240ms, then every 240ms, as with delivery jitter.
    while (source < audio.size() || !queue.empty() || ticks < (audio.size() + 16000) / 320) {
        std::this_thread::sleep_until(start + std::chrono::milliseconds(20 * ++ticks));
        if (ticks % 12 == 0 && source < audio.size()) {
            const auto count = std::min<size_t>(3840, audio.size() - source);
            require(asr_audio::append_audio(queue, std::span<const float>(audio.data() + source, count)) == 0, "delivery burst overflow");
            source += count;
        }
        std::array<float, 320> pcm{}, mixed{};
        consumed += asr_audio::pop_audio(queue, std::span<float>(pcm));
        // Local route ignores the identical server echo and includes mic once.
        asr_audio::add_remote(mixed, pcm, local);
        if (local) asr_audio::add(mixed, pcm);
        require(mixed == pcm, "self echo duplicated or input lost");
        asr_audio::normalize(mixed);
        const auto pushed = plugin_asr_stream_push(stream, mixed.data(), static_cast<uint32_t>(mixed.size()));
        if (pushed != PLUGIN_ASR_OK) throw std::runtime_error("ASR input status=" + std::to_string(pushed));
        poll();
    }
    require(consumed == audio.size(), "PCM lost before plugin");
    require(plugin_asr_stream_flush(stream) == PLUGIN_ASR_OK, "flush failed");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!drained && std::chrono::steady_clock::now() < deadline) {
        poll(); std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(drained && finals && nonempty, "no final/nonempty subtitle or drain timed out");
    std::cout << "PASS route=" << (local ? "local" : "remote") << " samples=" << consumed
              << " finals=" << finals << " nonempty=" << nonempty << " GAP=0 ERROR=0\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
