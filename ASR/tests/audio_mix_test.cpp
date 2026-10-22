#include "../../ScreenTrans_Client/include/AsrAudio.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <boost/circular_buffer.hpp>
static void check(bool ok) { if (!ok) throw std::runtime_error("Audio routing regression"); }
int main() try {
    // Device/TCP callbacks can deliver 240 ms at once, although the consumer
    // runs every 20 ms. This is continuous PCM, not grounds for a VAD reset.
    boost::circular_buffer<float> burst(16000);
    std::vector<float> packet(3840, .25f);
    check(asr_audio::append_audio(burst, packet) == 0);
    std::array<float, 320> tick{};
    size_t consumed = 0;
    for (int i = 0; i < 12; ++i) consumed += asr_audio::pop_audio(burst, tick);
    if (consumed != packet.size()) throw std::runtime_error("240ms delivery burst lost continuous PCM before VAD");
    check(burst.empty());
    packet.resize(20000);
    check(asr_audio::append_audio(burst, packet) == 4000);
    std::array<float, 4> playback{}, self{.25f, -.25f, .25f, -.25f}, remote{.125f, .125f, .125f, .125f};
    asr_audio::add_remote(playback, self, true); // server echo must not add self
    asr_audio::add_remote(playback, remote, false);
    auto recognition = playback;
    asr_audio::add(recognition, self); // one local capture, not two copies
    check(playback == remote);
    check(recognition[0] == .375f && recognition[1] == -.125f);
    std::array<float, 2> short_frame{.125f, -.125f};
    asr_audio::add(recognition, short_frame);
    check(recognition[2] == .375f); // missing frames stay on the silence timeline
    std::array<float, 2> loud{2.f, -1.f};
    asr_audio::normalize(loud);
    check(loud[0] == 1.f && loud[1] == -.5f);
    std::array<float, 4> silence{};
    asr_audio::normalize(silence);
    check(silence[0] == 0.f);
    asr_audio::CaptureGate gate;
    std::vector<float> captured{.25f};
    gate.filter(captured, false); check(captured.empty());
    captured = {.25f}; gate.filter(captured, true); check(captured.empty());
    captured = {.5f}; gate.filter(captured, true); check(captured.size() == 1);
    gate.filter(captured, false); check(captured.empty());
    std::cout << "audio routing: self echo, local inclusion, short frames, normalization passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n'; return 1;
}
