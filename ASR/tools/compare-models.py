"""Sequential CPU replay matrix. Synthetic input is diagnostic, not meeting CER."""
import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--dll',required=True);p.add_argument('--models',required=True)
    p.add_argument('--wav',required=True);p.add_argument('--reference',required=True);p.add_argument('--output-dir',required=True)
    p.add_argument('--baseline-dll');p.add_argument('--resume',action='store_true');a=p.parse_args()
    out=Path(a.output_dir);out.mkdir(parents=True,exist_ok=True)
    variants=[(model,threads,a.dll,'quality') for model in ['base','base-q5_1','small-q5_1','small'] for threads in [4,8]]
    if a.baseline_dll:variants.insert(0,('base',8,a.baseline_dll,'original'))
    variants.insert(1,('base',8,a.dll,'link-only'))
    summaries=[]
    for model,threads,dll,label in variants:
        name=f'{label}-{model}-{threads}';target=out/f'{name}.json'
        cmd=[sys.executable,str(Path(__file__).with_name('benchmark.py')),'--dll',dll,'--model',str(Path(a.models)/f'ggml-{model}.bin'),
             '--vad',str(Path(a.models)/'ggml-silero-v6.2.0.bin'),'--wav',a.wav,'--reference',a.reference,'--threads',str(threads),'--cpu','--output',str(target)]
        env=os.environ.copy()
        if label=='link-only':env.update(FLEXMEET_ASR_HIGHPASS_HZ='0',FLEXMEET_ASR_QUALITY='0',FLEXMEET_ASR_HYSTERESIS='0')
        if a.resume and target.exists():
            report=json.loads(target.read_text(encoding='utf-8'));report['variant']=name;report['run_failed']=bool(report.get('failures'));summaries.append(report);continue
        if target.exists():target.unlink()  # never reuse a stale report after a failed fresh run
        print('Running '+name,flush=True)
        with (out/f'{name}.log').open('w',encoding='utf-8') as log:
            result=subprocess.run(cmd,env=env,stdout=log,stderr=log,timeout=180)
        if not target.exists():raise RuntimeError(f'{name} produced no report; inspect log')
        report=json.loads(target.read_text(encoding='utf-8'));report['variant']=name;report['run_failed']=bool(result.returncode);summaries.append(report)
    (out/'matrix.json').write_text(json.dumps(summaries,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Matrix complete: '+str(out/'matrix.json'))
if __name__=='__main__':main()
