// Exercise the production queue/API with a deliberately blocked decoder.
// No model or device is opened; the backend seam supplies deterministic speech.
#include "../src/stream_api.cpp"
#include <future>
#include <iostream>

static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct SlowStream : plugin_asr_stream {
    std::mutex gate;
    std::condition_variable entered;
    bool blocked = false, release = false;
    std::atomic<int> frames{0};
    SlowStream() { plugin_asr_stream_default_config(&config); }
    ~SlowStream() {
        stopping.store(true); generation.fetch_add(1); ready.notify_all();
        if (worker.joinable()) worker.join();
    }
    void start() { worker = std::thread([this] { run(); }); }
    bool speech(const float* pcm) override { ++frames; return pcm[0] != 0; }
    void reset_vad() override {}
    std::string decode(const asr::DecodeRequest& r) override {
        std::unique_lock lock(gate); blocked = true; entered.notify_all();
        while (!release && !cancelled()) entered.wait_for(lock, std::chrono::milliseconds(5));
        return r.pcm.back() > .3f ? "new new" : "old old";
    }
    void wait_blocked() {
        std::unique_lock lock(gate);
        check(entered.wait_for(lock, std::chrono::seconds(3), [&] { return blocked; }), "decoder never started");
    }
    void unblock() { std::lock_guard lock(gate); release = true; entered.notify_all(); }
};
static std::vector<asr::Event> drain(plugin_asr_stream* s) {
    std::vector<asr::Event> events;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
        plugin_asr_result r{}; r.struct_size = sizeof(r); char text[1024];
        const int status = plugin_asr_stream_poll(s, &r, text, sizeof(text));
        check(status >= 0, "poll failed");
        if (status == PLUGIN_ASR_EVENT) {
            asr::Event e; e.kind = r.type; e.start = r.start_sample; e.end = r.end_sample; e.text = text;
            events.push_back(e);
            if (r.type == PLUGIN_ASR_DRAINED) return events;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("worker drain timed out");
}
int main() {
    try {
        std::vector<float> old(640 * 16, .25f), fresh(640 * 16, .5f);
        {
            SlowStream stream;
            plugin_asr_stream_reset(&stream);
            stream.fail("exception from old generation");
            check(!stream.failed && stream.output.empty(), "old exception poisoned new meeting");
        }
        {
            SlowStream stream; stream.start();
            check(plugin_asr_stream_push(&stream, old.data(), old.size()) == 0, "initial push failed");
            stream.wait_blocked();
            auto producer = std::async(std::launch::async, [&] { return plugin_asr_stream_push(&stream, fresh.data(), fresh.size()); });
            check(producer.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready, "push waited for inference");
            check(producer.get() == 0, "concurrent push failed");
            plugin_asr_stream_flush(&stream); stream.unblock();
            auto events = drain(&stream);
            int finals = 0;
            for (const auto& e : events) if (e.kind == asr::Event::final) {
                ++finals; check(e.end == 1280 * 16 && e.text == "new new", "audio appended during decode was lost");
            }
            check(finals == 1 && stream.frames == 40, "audio conservation/final count failed");
        }
        {
            SlowStream stream; stream.start();
            plugin_asr_stream_push(&stream, old.data(), old.size()); stream.wait_blocked();
            plugin_asr_stream_reset(&stream);
            plugin_asr_stream_push(&stream, fresh.data(), fresh.size());
            plugin_asr_stream_flush(&stream); stream.unblock();
            auto events = drain(&stream);
            for (const auto& e : events) {
                check(e.text != "old old", "reset published old generation");
                if (e.kind == asr::Event::final) check(e.start == 0 && e.end == 640 * 16, "reset timeline incorrect");
            }
        }
        {
            SlowStream stream; stream.config.queue_ms = 1000; stream.start();
            plugin_asr_stream_push(&stream, old.data(), old.size()); stream.wait_blocked();
            plugin_asr_stream_push(&stream, old.data(), old.size());
            check(plugin_asr_stream_push(&stream, fresh.data(), fresh.size()) == PLUGIN_ASR_OVERFLOW, "overflow not reported");
            plugin_asr_stream_flush(&stream); stream.unblock();
            auto events = drain(&stream);
            bool gap = false, final = false;
            for (const auto& e : events) {
                gap |= e.kind == asr::Event::gap;
                if (e.kind == asr::Event::final) {
                    final = true;
                    check(e.start == 1280 * 16 && e.end == 1920 * 16, "overload joined noncontiguous audio");
                }
            }
            check(gap && final, "overload did not recover");
        }
        std::cout << "worker: concurrent input conservation, cancellation, overload recovery passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
