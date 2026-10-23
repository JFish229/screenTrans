"""Compare aligned canonical float32 trace blocks; export PCM16 WAV previews."""
import argparse
import array
import json
import struct
import sys
import wave
from pathlib import Path

def read(index, tag, source=None):
    index=Path(index); data=index.with_suffix('.f32').read_bytes(); records={}
    for line in index.read_text(encoding='utf-8').splitlines():
        r=json.loads(line)
        if r['dropped_records']: raise ValueError('Trace dropped records; recapture with less disk load')
        if r['tag']!=tag or (source is not None and r['source']!=source):continue
        n=r['count']*4; b=data[r['offset']:r['offset']+n]
        if len(b)!=n:raise ValueError('Truncated trace data')
        key=(r['source'],r['epoch'],r['seq'],r['start'])
        if key in records:raise ValueError('Duplicate block '+str(key))
        records[key]=b
    if not records:raise ValueError('No matching blocks')
    return records
def main():
    p=argparse.ArgumentParser();p.add_argument('--left',required=True);p.add_argument('--left-tag',default='sent')
    p.add_argument('--right');p.add_argument('--right-tag',default='received');p.add_argument('--source',type=int);p.add_argument('--wav-dir')
    a=p.parse_args();left=read(a.left,a.left_tag,a.source)
    if a.right:
        right=read(a.right,a.right_tag,a.source)
        missing=set(left)-set(right);extra=set(right)-set(left)
        changed=[k for k in left.keys()&right.keys() if left[k]!=right[k]]
        report={'left_blocks':len(left),'right_blocks':len(right),'missing':len(missing),'extra':len(extra),'changed':len(changed),'bit_identical':not(missing or extra or changed)}
        print(json.dumps(report))
        if missing or extra or changed:sys.exit(1)
    if a.wav_dir:
        root=Path(a.wav_dir);root.mkdir(parents=True,exist_ok=True);streams={}
        for (source,epoch,seq,start),b in sorted(left.items()):
            stream=streams.setdefault((source,epoch),[])
            floats=array.array('f');floats.frombytes(b)
            if sys.byteorder!='little':floats.byteswap()
            stream.append((start,floats))
        for (source,epoch),blocks in streams.items():
            pcm=array.array('h');cursor=0
            for start,floats in blocks:
                if start!=cursor:raise ValueError('Discontinuous timeline: refuses to conceal gap/duplicate in WAV')
                pcm.extend(round(max(-1,min(1,f))*32767) for f in floats);cursor+=len(floats)
            if sys.byteorder!='little':pcm.byteswap()
            with wave.open(str(root/f'{source}-{epoch}.wav'),'wb') as w:w.setparams((1,2,16000,0,'NONE','not compressed'));w.writeframes(pcm.tobytes())
if __name__=='__main__':main()
