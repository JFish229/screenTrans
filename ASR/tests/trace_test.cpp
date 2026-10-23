#include "../../screenTrans/include/asr_trace.hpp"
int main() {
    asr_trace::Writer sender("sender"),receiver("receiver");
    if(!sender.enabled() || !receiver.enabled()) return 1;
    for(int i=0;i<100;++i) {
        std::vector<float> pcm(320);for(int j=0;j<320;++j) pcm[j]=float(std::sin((i*320+j)*.07)*.1);
        sender.record("sent",1,5,i,i*320,pcm,"Unicode: \xe5\xa5\xbd");
        receiver.record("received",1,5,i,i*320,pcm);
    }
}
