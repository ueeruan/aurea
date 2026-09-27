"""Generate our own deterministic Android tracking fixture (no external media)."""
from pathlib import Path
from PIL import Image, ImageDraw
import numpy as np, subprocess
root=Path(__file__).resolve().parents[1]
output=root/'android/app/src/androidTest/assets/motion-fixture.mp4'
output.parent.mkdir(parents=True,exist_ok=True)
rng=np.random.default_rng(7642)
a=rng.integers(40,220,(100,150,3),dtype=np.uint8)
base=Image.fromarray(a).resize((600,400),Image.Resampling.BICUBIC)
d=ImageDraw.Draw(base)
for _ in range(70):
 x,y=rng.integers(20,550),rng.integers(20,350);d.rectangle((x,y,x+12,y+12),fill=tuple(map(int,rng.integers(0,255,3))))
ff=str(root/'build/media-tools/imageio_ffmpeg/binaries/ffmpeg-win-x86_64-v7.1.exe')
p=subprocess.Popen([ff,'-v','error','-f','rawvideo','-pix_fmt','rgb24','-s','480x320','-r','30','-i','-','-c:v','libx264','-preset','fast','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart','-y',str(output)],stdin=subprocess.PIPE)
for i in range(60):
 x=40+i*.25+4*np.sin(i*1.3);y=40+3*np.cos(i*1.7)
 frame=base.transform((480,320),Image.Transform.AFFINE,(1,0,x,0,1,y),Image.Resampling.BICUBIC)
 p.stdin.write(frame.tobytes())
p.stdin.close();assert p.wait()==0;print(output)
