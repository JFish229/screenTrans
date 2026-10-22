#include "../src/stream_engine.hpp"
#include <iostream>
#include <stdexcept>

static void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Fake : asr::Backend {
    bool cancel = false;
    bool allow_draft = true;
    bool allow_partial() override { return allow_draft; }
    int resets = 0, decodes = 0;
    std::vector<int64_t> from, through;
    std::vector<std::string> prompts;
    bool speech(const float* pcm) override { return pcm[0] != 0; }
    bool cancelled() const override { return cancel; }
    void reset_vad() override { ++resets; }
    std::string decode(const asr::DecodeRequest& r) override {
        ++decodes; from.push_back(r.from); through.push_back(r.through);
        prompts.push_back(r.prompt);
        return "repeat repeat"; // Legitimate repetition must survive.
    }
};
static void feed(asr::Engine& engine, int ms, float amplitude, int chunk = 160) {
    std::vector<float> pcm(chunk, amplitude);
    for (int left = ms * 16; left > 0;) {
        int n = std::min(left, chunk);
        engine.consume(pcm.data(), n); left -= n;
    }
}
int main() {
    try {
        Fake backend;
        std::vector<asr::Event> events;
        asr::Engine engine(backend, {}, [&](auto e) { events.push_back(e); });
        engine.reset();
        feed(engine, 1000, 0); check(events.empty(), "silence produced subtitles");
        feed(engine, 1100, .01f);
        check(!events.empty() && events.front().kind == asr::Event::partial, "no draft while speaking");
        auto id = events.front().id;
        for (auto& e : events) check(e.id == id && e.kind == asr::Event::partial, "draft became final early");
        feed(engine, 180, 0); check(events.back().kind == asr::Event::partial, "short pause split utterance");
        feed(engine, 500, .01f); feed(engine, 400, 0);
        check(events.back().kind == asr::Event::final, "missing endpoint final");
        check(events.back().id == id && events.back().text == "repeat repeat", "repeated words removed");
        const auto previous = events.size();
        feed(engine, 800, 0); engine.finish(); check(events.size() == previous, "duplicate final");
        feed(engine, 800, .01f); engine.finish();
        check(events.back().id > id && !backend.prompts.back().empty(), "no bounded confirmed context");
        engine.reset(); events.clear();
        // One huge batch should coalesce draft jobs but retain every final window.
        feed(engine, 18000, .5f, 18000 * 16); engine.finish();
        int finals = 0;
        int64_t boundary = 0;
        for (const auto& e : events) if (e.kind == asr::Event::final) {
            check(e.start == boundary, "rolling-window ownership has gap/overlap");
            boundary = e.end; ++finals;
        }
        check(finals == 3 && boundary == 18000 * 16, "long speech lost audio");
        engine.reset(); events.clear();
        feed(engine, 600, .5f); engine.reset(); events.clear(); feed(engine, 500, 0); engine.finish();
        check(events.empty(), "reset leaked prior meeting");
        // A slow final can leave new PCM waiting outside the current batch.
        // Skip obsolete drafts, retain all PCM, and still emit the final once.
        engine.reset(); events.clear(); backend.allow_draft = false;
        feed(engine, 1000, .5f);
        check(events.empty(), "stale draft decoded while newer input was waiting");
        backend.allow_draft = true; feed(engine, 32, .5f);
        check(events.size() == 1 && events.back().kind == asr::Event::partial, "draft did not resume after catch-up");
        engine.finish();
        check(events.size() == 2 && events.back().kind == asr::Event::final && events.back().end == 1032 * 16, "catch-up lost final PCM");
        engine.reset(); events.clear();
        backend.cancel = true; feed(engine, 2000, .5f); engine.finish();
        check(events.empty(), "cancelled work published");
        std::cout << "stream engine: segmentation, rolling boundaries, draft backpressure passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
