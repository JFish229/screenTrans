import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

p=argparse.ArgumentParser();p.add_argument('--exe',required=True);a=p.parse_args()
with tempfile.TemporaryDirectory(prefix='FlexMeet-音频-') as directory:
    root=Path(directory);env=os.environ.copy();env['FLEXMEET_ASR_TRACE_DIR']=directory
    subprocess.run([a.exe],env=env,check=True)
    left=next(root.glob('sender-*.jsonl'));right=next(root.glob('receiver-*.jsonl'))
    tool=Path(__file__).resolve().parents[1]/'tools'/'trace-audio.py'
    subprocess.run([sys.executable,str(tool),'--left',str(left),'--right',str(right),'--wav-dir',str(root/'wav')],check=True)
    assert len(list((root/'wav').glob('*.wav')))==1
    for path in [left,right]:
        records=[json.loads(x) for x in path.read_text(encoding='utf-8').splitlines()]
        assert records[-1]['tag']=='trace-end' and records[-1]['dropped_records']==0
    # A changed sample must fail the same comparison command.
    data=right.with_suffix('.f32');b=bytearray(data.read_bytes());b[100]^=1;data.write_bytes(b)
    result=subprocess.run([sys.executable,str(tool),'--left',str(left),'--right',str(right)])
    assert result.returncode==1
print('Unicode diagnostic paths, async conservation, WAV export and corruption detection passed')
