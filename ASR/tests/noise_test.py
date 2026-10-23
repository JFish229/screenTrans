"""Deterministic zero/DC/broadband/click inputs. Not real keyboard recordings."""
import argparse
import ctypes as C
import json
import math
import random
import sys
import time
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from asr_client import Client
def main():
    p=argparse.ArgumentParser();p.add_argument('--dll',required=True);p.add_argument('--model',required=True);p.add_argument('--vad',required=True);p.add_argument('--output',required=True);a=p.parse_args()
    client=Client(a.dll);h=client.open(client.config(a.model,a.vad,threads=8,gpu=False));rng=random.Random(741)
    cases={'zero':[0.]*32000,'dc':[.003]*32000,'noise':[rng.uniform(-.006,.006) for _ in range(32000)],'clicks':[.5*math.exp(-(i%8000)/20) if i%8000<100 else 0 for i in range(32000)]}
    report={}
    try:
        for name,samples in cases.items():
            client.reset(h);events=[];begin=time.monotonic()
            samples=samples+[0.]*8000
            for i in range(0,len(samples),320):
                due=begin+i/16000;time.sleep(max(0,due-time.monotonic()))
                pcm=(C.c_float*len(samples[i:i+320]))(*samples[i:i+320]);assert client.push(h,pcm,len(pcm))==0
                while True:
                    e=client.poll_event(h)
                    if not e:break
                    events.append(e)
            client.flush(h);deadline=time.monotonic()+30
            while not events or events[-1]['type']!=5:
                e=client.poll_event(h)
                if e:events.append(e)
                if time.monotonic()>deadline:raise TimeoutError(name)
                time.sleep(.005)
            assert not [e for e in events if e['type'] in (3,4) or (e['type'] in (1,2) and e['text'])],(name,events)
            report[name]={'nonempty_subtitles':0,'events':events}
    finally:client.destroy(h)
    Path(a.output).write_text(json.dumps(report,indent=2),encoding='utf-8');print('zero/DC/noise/clicks: no subtitles')
if __name__=='__main__':main()
