"""Launch the actual Release server; drive independent headless protocol peers.
This tests TCP/room behavior, not microphone devices or the graphical UI.
"""
import argparse
import math
import socket
import struct
import subprocess
import threading
import time
import json
from pathlib import Path

VERSION = 0x464d0002
def exact(s, n):
    b = b''
    while len(b) < n:
        part = s.recv(n-len(b))
        if not part: raise EOFError('disconnected')
        b += part
    return b
def recv(s):
    n, = struct.unpack('!i', exact(s, 4))
    assert 0 <= n <= 16*1024*1024
    return exact(s, n)
def send(s, b): s.sendall(struct.pack('!i', len(b))+b)
def num(s, fmt, n): send(s, struct.pack('!'+fmt,n))
def value(s, fmt): return struct.unpack('!'+fmt, recv(s))[0]
def captions(events):
    b=struct.pack('!Q',len(events))
    for kind,source,epoch,uid,rev,commit,text in events:
        text=text.encode('utf-8'); b+=struct.pack('!7Q',kind,source,epoch,uid,rev,commit,len(text))+text
    return b
def parse(b):
    n,=struct.unpack_from('!Q',b); pos=8; out=[]
    for _ in range(n):
        fields=struct.unpack_from('!7Q',b,pos);pos+=56
        text=b[pos:pos+fields[-1]].decode('utf-8');pos+=fields[-1]
        out.append((*fields[:-1],text))
    assert pos==len(b)
    return out
def connect(port, kind=0x20, version=VERSION):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    num(s,'B',kind);num(s,'I',version)
    assert value(s,'?') == (version==VERSION)
    return s
class Peer:
    def __init__(self, port, room=None):
        self.main=connect(port);self.id=value(self.main,'Q')
        self.audio=connect(port,0x21);num(self.audio,'Q',self.id);assert value(self.audio,'?')
        num(self.main,'?',False);num(self.main,'i',1 if room is None else 0)
        num(self.main,'?',False)
        if room is None:
            num(self.main,'I',1234);self.room=value(self.main,'I')
        else:
            num(self.main,'I',room);num(self.main,'I',1234)
            assert value(self.main,'?') and value(self.main,'?')
            assert value(self.audio,'?') and value(self.main,'?');self.room=room
        self.events={};self.errors=[];self.lock=threading.Lock();self.frames=0;self.stop=False;self.audio_position=0;self.hosts=set()
        self.thread=threading.Thread(target=self.read,daemon=True);self.thread.start()
    def frame(self, events=(), transfer=None):
        num(self.main,'Q',self.id);send(self.main,b'test');num(self.main,'?',transfer is not None)
        if transfer is not None:
            num(self.main,'I',0);num(self.main,'Q',2**64-1);num(self.main,'Q',transfer)
        num(self.main,'?',False);send(self.main,captions(events));num(self.main,'Q',self.id);num(self.main,'?',False)
    def read(self):
        try:
            while not self.stop:
                signal=value(self.main,'b');source=value(self.main,'Q')
                if signal&1: continue
                recv(self.main);host=value(self.main,'?')
                if host and source==self.id: value(self.main,'I')
                if value(self.main,'?'): value(self.main,'Q')
                assert not value(self.main,'?')
                events=parse(recv(self.main));assert not value(self.main,'?')
                with self.lock:
                    self.frames+=1
                    if host:self.hosts.add(source)
                    else:self.hosts.discard(source)
                    for e in events:
                        if e[0]==2 and e[5]: self.events.setdefault(e[5],(e,time.perf_counter()))
        except (OSError,EOFError) as e:
            if not self.stop:self.errors.append(str(e))
    def finals(self):
        with self.lock:return [self.events[k][0] for k in sorted(self.events)]
    def audio_send(self, samples, seq):
        for fmt,v in [('Q',999999),('B',1),('Q',100),('Q',seq),('Q',self.audio_position)]:num(self.audio,fmt,v)
        send(self.audio,struct.pack('!%df'%len(samples),*samples));self.audio_position+=len(samples)
    def audio_read(self):
        fields=[value(self.audio,f) for f in ['Q','B','Q','Q','Q']]
        return fields,recv(self.audio) if fields[1]&1 else b''
    def close(self):
        self.stop=True
        for s in [self.main,self.audio]:
            try:s.shutdown(socket.SHUT_RDWR)
            except OSError:pass
            s.close()

def wait(test, seconds=5):
    end=time.monotonic()+seconds
    while not test():
        if time.monotonic()>end:raise AssertionError('condition timeout')
        time.sleep(.005)
def main():
    p=argparse.ArgumentParser();p.add_argument('--server',required=True);p.add_argument('--output',required=True);p.add_argument('--dll');p.add_argument('--model');p.add_argument('--vad');p.add_argument('--wav');a=p.parse_args()
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    output=Path(a.output);output.parent.mkdir(parents=True,exist_ok=True)
    log=output.with_suffix('.server.log').open('wb')
    process=subprocess.Popen([str(Path(a.server).resolve())],stdin=subprocess.PIPE,stdout=log,stderr=log)
    process.stdin.write(f'{port}\n'.encode());process.stdin.flush()
    peers=[]
    try:
        time.sleep(.4)
        wrong=connect(port,version=999);wrong.close()
        x=Peer(port);peers.append(x);x.frame()
        y=Peer(port,x.room);peers.append(y);y.frame();x.frame()
        latency=[]
        for source in [x,y]:
            for i in range(1,7):
                event=(2,999999,100,i,2,999999,f'authoritative {source.id} {i}')
                source.frame([(1,999999,100,i,1,0,'draft')]);start=time.perf_counter();source.frame([event,event])
                expected=(i if source is x else i+6)
                wait(lambda:len(x.finals())==expected and len(y.finals())==expected)
                assert x.finals()==y.finals() and x.finals()[-1][1]==source.id
                with y.lock:latency.append((y.events[expected][1]-start)*1000)
                source.frame([(1,999999,99,i,100,0,'late old draft')])
        for i,count in enumerate([3840,8000,160,512,7999]):
            samples=[math.sin(n*.07)*.1 for n in range(count)];samples[0]=-0.0
            x.audio_send(samples,i)
            for peer in [x,y]:
                fields,b=peer.audio_read();assert fields[0]==x.id and b==struct.pack('!%df'%count,*samples)
        # A late join receives the same canonical final history, even without
        # selecting the speaker's screen and without microphone sharing.
        z=Peer(port,x.room);peers.append(z);z.frame();wait(lambda:len(z.finals())==12);assert z.finals()==x.finals()
        x.frame(transfer=y.id);wait(lambda:x.id not in x.hosts);y.frame();wait(lambda:y.id in y.hosts and y.id in x.hosts);x.frame();wait(lambda:x.id not in y.hosts)
        asr_finals=0
        if a.dll:
            import ctypes as C
            import array
            import wave
            import sys
            sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
            from asr_client import Client
            decoder=Client(a.dll);handle=decoder.open(decoder.config(a.model,a.vad,threads=8,gpu=False))
            with wave.open(a.wav,'rb') as wav:
                assert (wav.getnchannels(),wav.getframerate(),wav.getsampwidth())==(1,16000,2)
                samples=array.array('h',wav.readframes(wav.getnframes()));samples.extend([0]*8000)
            try:
                for source in [x,y]:
                    decoder.reset(handle);begin=time.monotonic();completed=[]
                    def poll():
                        drained=False
                        while True:
                            event=decoder.poll_event(handle)
                            if not event:return drained
                            assert event['type'] not in (3,4),event
                            if event['type'] in (1,2):
                                source.frame([(event['type'],0,101,event['utterance_id'],event['revision'],0,event['text'])])
                                if event['type']==2 and event['text']:completed.append(event['text'])
                            drained|=event['type']==5
                    for i in range(0,len(samples),320):
                        due=begin+i/16000
                        while time.monotonic()<due:poll();time.sleep(.002)
                        raw=samples[i:i+320];pcm=(C.c_float*len(raw))(*(v/32768 for v in raw));assert decoder.push(handle,pcm,len(pcm))==0;poll()
                    decoder.flush(handle);deadline=time.monotonic()+30
                    while not poll():
                        assert time.monotonic()<deadline
                        time.sleep(.005)
                    assert completed,'ASR produced no finals'
                    asr_finals+=len(completed)
                    wait(lambda:len(y.finals())==12+asr_finals and x.finals()==y.finals())
                    assert [e[-1] for e in x.finals()[-len(completed):]]==completed
            finally:decoder.destroy(handle)
        # A half PCM packet must not forward old payload from the prior frame.
        for fmt,v in [('Q',x.id),('B',1),('Q',100),('Q',99),('Q',1000)]:num(x.audio,fmt,v)
        x.audio.sendall(struct.pack('!i',400)+b'\x00'*12);x.audio.shutdown(socket.SHUT_WR)
        wait(lambda:bool(x.errors));assert len(y.finals())==12+asr_finals
        # A separate source with non-finite PCM is disconnected before broadcast.
        bad=Peer(port,y.room);peers.append(bad);bad.frame();bad.audio_send([float('nan')],0);wait(lambda:bool(bad.errors))
        rejoined=Peer(port,y.room);peers.append(rejoined);rejoined.frame()
        wait(lambda:rejoined.finals()==y.finals())
        assert rejoined.id not in {x.id,y.id,z.id,bad.id},'source ID reused'
        y.frame();wait(lambda:y.id in rejoined.hosts)
        report={'tests':'protocol mismatch, canonical source, duplicate/stale events, both speakers, late join, host transfer, exact PCM at 240/500ms, truncated PCM, NaN', 'real_dll_finals':asr_finals,'finals_identical':True,'finals':len(y.finals()),'sync_ms':{'p50':sorted(latency)[len(latency)//2],'p95':max(latency),'n':len(latency)}}
        output.write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))
    finally:
        for peer in peers:peer.close()
        process.terminate();process.wait(timeout=5);log.close()
if __name__=='__main__':main()
