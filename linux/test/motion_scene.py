#!/usr/bin/env python3
"""Deterministic Motion fixture with exact backward vectors/depth, no game assets.

Thin blue handle, independent foreground/background motion, camera pan/rotation,
disocclusion, cut, invalid vectors, and a static tail. Optional stress faults do
not alter the real-frame cadence. Source color is SDR in the model's domain.
"""
import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def generate(out, width=640, height=360, frames=24):
    out.mkdir(parents=True, exist_ok=False)
    y,x=np.mgrid[:height,:width].astype(np.float32)
    previous_camera=previous_object=previous_angle=0
    previous_mask=np.zeros((height,width),bool)
    metadata={'scene':'Motion (procedural Vulkan regression)', 'extent':[width,height], 'frames':frames,
              'depth':'device depth, forward Z', 'motion':'current to previous, normalized UV',
              'cut_frames':[13], 'invalid_motion_frames':[15], 'cases':[]}
    for i in range(frames):
        camera=0 if i<2 else (i-1)*width/160
        if 8<=i<=12: camera+=(i-7)*width/20
        if i>=13: camera+=width/3
        if i>=18: camera=width*0.6
        obj=width*(0.25+0.012*min(i,18))
        angle=(i-7)*0.18 if 8<=i<=12 else (-0.35 if i>=13 else 0)
        dx=x-width/2;dy=y-height/2
        world=np.cos(angle)*dx-np.sin(angle)*dy+width/2+camera
        wy=np.sin(angle)*dx+np.cos(angle)*dy+height/2
        wall=(0.28+0.06*np.sin(world*0.16)+0.035*np.cos(wy*0.2))
        rgb=np.stack((wall*1.12,wall*0.96,wall*0.78),axis=-1)
        rgb+=(((world.astype(int)//24+wy.astype(int)//24)%2)*0.035)[...,None]
        sky=wy<height*.28
        rgb[sky]=np.stack((0.48+0.03*np.sin(world[sky]*.01),np.full(sky.sum(),.57),np.full(sky.sum(),.66)),axis=-1)
        center=obj+width*.25
        handle=(np.abs(world-center)<max(2,width/160)) & (wy>height*.31) & (wy<height*.81)
        blade=(np.abs(world-center+(wy-height*.31)*.18)<max(1,width/320)) & (wy>height*.12) & (wy<=height*.31)
        mask=handle|blade
        rgb[handle]=(0.035,.09,.55)
        rgb[blade]=(.6,.65,.7)
        depth=np.where(sky,1.0,np.where(mask,.35,.78)).astype(np.float32)
        motion=np.zeros((height,width,2),np.float32)
        def previous_screen(world_x):
            px=world_x-previous_camera-width/2;py=wy-height/2
            return np.cos(previous_angle)*px+np.sin(previous_angle)*py+width/2, -np.sin(previous_angle)*px+np.cos(previous_angle)*py+height/2
        px,py=previous_screen(world)
        motion[...,0]=(px-x)/width;motion[...,1]=(py-y)/height
        ox,oy=previous_screen(world+previous_object-obj)
        motion[mask,0]=(ox[mask]-x[mask])/width;motion[mask,1]=(oy[mask]-y[mask])/height
        if i>=13: rgb=rgb[...,::-1].copy()  # new color domain, explicit cut
        if i==15: motion[height//3:height//2,width//3:width//2]=np.nan
        rgba=np.concatenate((np.clip(rgb,0,1),np.ones((height,width,1))),axis=-1)
        rgba8=np.rint(rgba*255).astype(np.uint8)
        rgba8.tofile(out/f'in-{i:02}.rgba8')
        motion.tofile(out/f'motion-{i:02}.f32')
        depth.tofile(out/f'depth-{i:02}.f32')
        # Regions for stale-blue error: where the object used to be, excluding
        # its current silhouette. Used with a full-NR reference, never raw input.
        ix=np.clip(np.rint(px).astype(int),0,width-1);iy=np.clip(np.rint(py).astype(int),0,height-1)
        exposed=previous_mask[iy,ix] & ~mask & (px>=0) & (px<width) & (py>=0) & (py<height)
        exposed.astype(np.uint8).tofile(out/f'exposed-{i:02}.u8')
        Image.fromarray(rgba8).save(out/f'in-{i:02}.png')
        metadata['cases'].append({'frame':i,'camera':camera,'camera_angle_rad':angle,'object':obj,'cut':i==13,
                                  'invalid_motion':i==15,'exposed_pixels':int(exposed.sum())})
        previous_camera,previous_object,previous_angle,previous_mask=camera,obj,angle,mask
    (out/'scene.json').write_text(json.dumps(metadata,indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('out',type=Path)
    p.add_argument('--width',type=int,default=640)
    p.add_argument('--height',type=int,default=360)
    a=p.parse_args();generate(a.out,a.width,a.height)
