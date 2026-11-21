#!/usr/bin/env python3
"""Interleaved GPU timings on a supplied Motion fixture. No game FPS claims.

The harness uploads/downloads outside its timestamp pair; idle gaps and automatic
clocks still affect results. The last 12 frames contain whole N=2/3/4 periods.
Run without simultaneous builds/other GPU tests. Legacy is an optional preserved
old runtime test binary, never part of the shipped RDNA3 implementation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from bench_rdna3 import sensors


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--scene',type=Path,required=True)
    ap.add_argument('--installed',type=Path,required=True)
    ap.add_argument('--runner',type=Path,required=True)
    ap.add_argument('--legacy',type=Path)
    ap.add_argument('--rounds',type=int,default=3)
    ap.add_argument('--out',type=Path,required=True)
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=False)
    if a.rounds<2:ap.error('at least two rounds')
    scene=json.loads((a.scene/'scene.json').read_text());w,h=scene['extent'];assert scene['frames']==24
    cases={'full':(1,a.runner),'n2':(2,a.runner),'n3':(3,a.runner),'n4':(4,a.runner)}
    if a.legacy:cases['legacy']=(2,a.legacy)
    report={'method':__doc__,'scene':scene,'runs':[], 'source':subprocess.check_output(['git','rev-parse','HEAD']).decode().strip(),
            'binary_sha256':{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in [a.runner]+([a.legacy] if a.legacy else [])}}
    env=dict(os.environ,VK_LOADER_LAYERS_DISABLE='~implicit~',DLSSNR_MODEL='CC_SMALL_FP8',RUN_SEQUENCE_CUT='13',RUN_SEQUENCE_DEPTH=str(a.scene/'depth-%02d.f32'))
    for round_index in range(a.rounds):
        order=list(cases)
        if round_index%2:order.reverse()
        for key in order:
            n,runner=cases[key];out=a.out/f'{round_index}-{key}';out.mkdir()
            row={'round':round_index,'case':key,'sensors_before':sensors()}
            with (out/'run.log').open('w') as log:
                r=subprocess.run([str(runner.resolve()),str(a.installed),str(w),str(h),'24',str(n),str(a.scene/'in-%02d.rgba8'),str(a.scene/'motion-%02d.f32'),str(out/'out-%02d.rgba8')],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=180)
            row['sensors_after']=sensors();assert r.returncode==0,(key,r.returncode)
            text=(out/'run.log').read_text();assert not any(x in text for x in ['persist error:','chain error:','output not trustworthy'])
            rows=re.findall(r'^\s*(\d+)\s+(yes|no)\s+(yes|no)\s+(\d+)\s+([0-9.]+)$',text,re.M);assert len(rows)==24
            timing={int(i):float(t) for i,t in re.findall(r'gpu frame (\d+) ([0-9.]+) ms',text)};assert len(timing)==24
            if key!='legacy':
                assert all((int(d)>0)==(int(i)%n==0) and g=='no' for i,_,g,d,_ in rows)
            row['frames']=[{'frame':int(i),'full':int(d)>0,'gpu_ms':timing[int(i)]} for i,_,_,d,_ in rows]
            warm=row['frames'][12:]
            row['mean_ms']=statistics.mean(r['gpu_ms'] for r in warm)
            row['full_ms']=statistics.mean(r['gpu_ms'] for r in warm if r['full'])
            skip=[r['gpu_ms'] for r in warm if not r['full']];row['reconstruction_ms']=statistics.mean(skip) if skip else None
            report['runs'].append(row)
            (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
            print(round_index+1,key,round(row['mean_ms'],4),'ms',flush=True)
    report['summary']={}
    for key in cases:
        rows=[r for r in report['runs'] if r['case']==key];times=[r['mean_ms'] for r in rows]
        report['summary'][key]={'median_ms':statistics.median(times),'min_ms':min(times),'max_ms':max(times),
            'full_median_ms':statistics.median(r['full_ms'] for r in rows),
            'reconstruction_median_ms':statistics.median(r['reconstruction_ms'] for r in rows) if key!='full' else None}
    (a.out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report['summary'],indent=2))


if __name__=='__main__':main()
