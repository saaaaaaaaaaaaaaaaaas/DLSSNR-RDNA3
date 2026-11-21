#!/usr/bin/env python3
"""Score Motion outputs against full NR, and export native-size diagnostic crops.

PSNR and exposed-pixel blue error measure this fixture only. They cannot qualify
a real game's sword motion, FG presentation or all HDR encodings.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

import numpy as np
from PIL import Image,ImageDraw


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('root',type=Path)
    ap.add_argument('--scene',default='motion')
    args=ap.parse_args();root=args.root
    scene=json.loads((root/args.scene/'scene.json').read_text())
    w,h=scene['extent'];frames=scene['frames']
    def load(directory,i):
        return np.fromfile(root/directory/f'out-{i:02}.rgba8',np.uint8).reshape(h,w,4)
    report={'scene':scene,'reference':'full-NR Turbo on every real frame, not independent game ground truth','cases':{}}
    full=[load('motion-full',i) for i in range(frames)]
    for name,n in [('motion-full',1),('motion-n2',2),('motion-n3',3),('motion-n4',4),('motion-legacy',2)]:
        folder=root/name
        if not folder.exists():continue
        log=(folder/'run.log').read_text()
        rows=re.findall(r'^\s*(\d+)\s+(yes|no)\s+(yes|no)\s+(\d+)\s+([0-9.]+)$',log,re.M)
        assert len(rows)==frames,(name,len(rows))
        if name!='motion-legacy':
            for i,reused,gated,dispatches,_ in rows:
                assert (int(dispatches)>0)==(int(i)%n==0),(name,i,'cadence')
                assert gated=='no'
        timings={int(i):float(ms) for i,ms in re.findall(r'gpu frame (\d+) ([0-9.]+) ms',log)}
        metrics=[]
        for i in range(frames):
            raw=load(name,i);rgb=raw[...,:3].astype(np.float64);ref=full[i][...,:3].astype(np.float64)
            assert (raw[...,3]==255).all(),(name,i,'alpha')
            error=rgb-ref;mse=float(np.square(error).mean())
            exposed=np.fromfile(root/args.scene/f'exposed-{i:02}.u8',np.uint8).reshape(h,w).astype(bool)
            blue=error[...,2]-0.5*(error[...,0]+error[...,1])
            metrics.append({'frame':i,'full':int(rows[i][3])>0,'sha256':hashlib.sha256(raw.tobytes()).hexdigest(),
                            'psnr_db':None if mse==0 else 10*math.log10(255**2/mse),'rmse_rgb8':math.sqrt(mse),
                            'exposed_positive_blue_error_rgb8':float(np.maximum(blue[exposed],0).mean()) if exposed.any() else None,
                            'exposed_mae_rgb8':float(abs(error)[exposed].mean()) if exposed.any() else None,
                            'gpu_ms':timings.get(i)})
        warm=[r for r in metrics if r['frame']>=4]
        # Twelve frames contain whole periods for N=2,3,4. Excluding just four
        # frames would bias N=3's average toward a different full/skip ratio.
        timed=[r for r in metrics if r['frame']>=12]
        skips=[r['gpu_ms'] for r in timed if not r['full'] and r['gpu_ms'] is not None]
        runs=[r['gpu_ms'] for r in timed if r['full'] and r['gpu_ms'] is not None]
        def mean(values):return statistics.mean(values) if values else None
        report['cases'][name]={'full_launches':sum(r['full'] for r in metrics),'motion_gated':sum(r[2]=='yes' for r in rows),
            'warmup_frames_excluded':12,'quality_first_frame':4,'gpu_average_ms':mean([r['gpu_ms'] for r in timed if r['gpu_ms'] is not None]),
            'gpu_full_average_ms':mean(runs),'gpu_reconstruction_average_ms':mean(skips),
            'exposed_blue_mean_rgb8':mean([r['exposed_positive_blue_error_rgb8'] for r in warm if r['exposed_positive_blue_error_rgb8'] is not None]),
            'exposed_mae_mean_rgb8':mean([r['exposed_mae_rgb8'] for r in warm if r['exposed_mae_rgb8'] is not None]),
            'frames':metrics}
    # An image and amplified error for the same moving frame, no generative edit.
    i=9;names=['motion-full','motion-n2','motion-legacy']
    sheet=Image.new('RGB',(w*2,(h+24)*len(names)),(20,20,20));draw=ImageDraw.Draw(sheet)
    for row,name in enumerate(names):
        rgb=load(name,i)[...,:3];y=row*(h+24)
        diff=np.minimum(abs(rgb.astype(np.int16)-full[i][...,:3])*8,255).astype(np.uint8)
        draw.text((8,y+5),name+' frame 9',fill='white');draw.text((w+8,y+5),'difference vs full NR x8',fill='white')
        sheet.paste(Image.fromarray(rgb),(0,y+24));sheet.paste(Image.fromarray(diff),(w,y+24))
    common=[i for i in range(4,frames) if not report['cases']['motion-n2']['frames'][i]['full'] and not report['cases']['motion-legacy']['frames'][i]['full']]
    report['common_skipped_comparison']={'frames':common}
    for name in ('motion-n2','motion-legacy'):
        values=[report['cases'][name]['frames'][i]['exposed_positive_blue_error_rgb8'] for i in common]
        report['common_skipped_comparison'][name]=statistics.mean(v for v in values if v is not None)
    sheet.save(root/'motion-comparison.png')
    (root/'motion-quality.json').write_text(json.dumps(report,indent=2)+'\n')
    for name,data in report['cases'].items():
        print(name,{k:v for k,v in data.items() if k!='frames'})


if __name__=='__main__':main()
