#include "../../screenTrans/include/captions.hpp"
#include "../src/audio_quality.hpp"
#include "../src/stream_engine.hpp"
#include <iostream>
#include <stdexcept>
void check(bool b,const char* why) { if(!b) throw std::runtime_error(why); }
struct Voice : asr::Backend {
    float p=0; int calls=0;
    bool speech(const float*) override {return p>.5f;}
    float probability(const float*) override {return p;}
    bool cancelled() const override {return false;}
    void reset_vad() override {}
    std::string decode(const asr::DecodeRequest& r) override {++calls; check(r.voiced_ms>=96,"short answer lost"); return "ok";}
};
int main() { try {
    captions::Store server,a,b;
    captions::Event e; e.source=1; e.epoch=10; e.id=1; e.revision=1; e.kind=captions::partial; e.text="draft";
    check(server.apply(e,true),"partial rejected"); auto old=server.snapshot();
    e.kind=captions::final; e.revision=2; e.text="same final";
    check(server.apply(e,true),"final rejected"); check(!server.apply(e,true),"duplicate final accepted");
    for(auto v:server.snapshot()) {a.apply(v); b.apply(v);}
    for(auto v:old) a.apply(v);
    check(a.render()==b.render(),"late draft overwrote final");
    e.id=2; e.revision=1; e.kind=captions::partial; e.text="cancel me"; server.apply(e,true);
    captions::Event cancel; cancel.source=1; cancel.epoch=11;
    server.apply(cancel,true); check(!server.apply(e,true),"old epoch accepted");
    for(auto v:server.snapshot()) {a.apply(v);b.apply(v);}
    check(a.render()==b.render() && a.render().find("cancel me")==std::string::npos,"cancel failed");
    for(int i=1;i<=40;++i) {e.epoch=11;e.id=i;e.revision=2;e.kind=captions::final;e.text=std::to_string(i);check(server.apply(e,true),"new final rejected");}
    e.id=41;e.kind=captions::partial;e.revision=1;e.text="rejected draft";server.apply(e,true);
    for(auto v:server.snapshot()) a.apply(v);
    e.kind=captions::final;e.revision=2;e.text.clear();server.apply(e,true);
    for(auto v:server.snapshot()) a.apply(v);
    check(a.render().find("rejected draft")==std::string::npos,"empty final failed to clear draft");
    e.id=42;e.kind=captions::partial;e.revision=1;e.text="departed draft";server.apply(e,true);
    for(auto v:server.snapshot()) a.apply(v);
    server.depart(1);for(auto v:server.snapshot()) a.apply(v);
    check(a.render().find("departed draft")==std::string::npos,"disconnect retained draft");
    auto wire=captions::encode(server.snapshot()); std::vector<captions::Event> parsed;
    check(captions::decode(wire,parsed),"wire roundtrip failed"); int finals=0; for(auto& v:parsed) finals+=v.kind==captions::final; check(finals==32,"history not bounded");
    wire.pop_back(); check(!captions::decode(wire,parsed),"half packet accepted");
    check(!captions::utf8(std::string("\xc0\x80",2)),"overlong UTF8 accepted");
    check(asr::reject("...",1000,.99f,20,.9f,.01f),"punctuation survived");
    const auto yes=reinterpret_cast<const char*>(u8"好");
    const auto repeat=reinterpret_cast<const char*>(u8"对对对");
    check(!asr::reject(yes,128,.98f,20,.9f,.01f),"confident short answer removed");
    check(!asr::reject(repeat,800,.98f,20,.9f,.01f),"real repetitions removed");
    check(asr::reject(repeat,800,.65f,1,.3f,.7f),"weak hallucination survived");
    asr::Highpass hp; std::vector<float> dc(16000,.1f);hp.apply(dc,80);check(std::abs(dc.back())<1e-5f,"DC not suppressed");
    Voice v; asr::Settings settings;settings.hysteresis=true;settings.min_ms=96;
    std::vector<asr::Event> events;asr::Engine engine(v,settings,[&](auto e){events.push_back(e);});engine.reset();
    std::vector<float> frame(512,.01f);
    v.p=.99;engine.consume(frame.data(),512);v.p=0;for(int i=0;i<12;++i)engine.consume(frame.data(),512);check(v.calls==0,"single click triggered decode");
    v.p=.99;for(int i=0;i<3;++i)engine.consume(frame.data(),512);v.p=0;for(int i=0;i<12;++i)engine.consume(frame.data(),512);check(v.calls==1 && events.back().kind==asr::Event::final,"96ms short answer not finalized");
    std::cout<<"caption ordering, stale epochs, history, malformed wire, quality and VAD passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;} }
