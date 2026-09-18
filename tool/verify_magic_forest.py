"""Decode the actual exported MP4 and verify all five moving shots."""
import sys, json, hashlib
from pathlib import Path
from PIL import Image, ImageStat, ImageDraw
folder=Path(__file__).resolve().parents[1]/'output/floresta-magica'
sys.path.insert(0,str(folder/'verification-tools'))
import imageio_ffmpeg

path=folder/'LUMEN-Floresta-Magica.mp4'
reader=imageio_ffmpeg.read_frames(str(path),pix_fmt='rgb24')
metadata=next(reader)
assert tuple(metadata['size'])==(720,1280),metadata
assert abs(metadata['fps']-30)<.01,metadata
assert abs(metadata['duration']-15)<.04,metadata
checks=[]; hashes={}; selected={};count=0
for i,frame in enumerate(reader):
    assert len(frame)==720*1280*3
    if i%90 in [0,45,89]:
        im=Image.frombytes('RGB',(720,1280),frame)
        small=im.resize((90,160))
        stat=ImageStat.Stat(small)
        assert max(stat.stddev)>10,(i,stat.stddev)
        hashes[i]=hashlib.sha256(frame).hexdigest()
        checks.append({'frame':i,'mean':stat.mean,'stddev':stat.stddev})
        if i%90==45:
            selected[i]=im
            im.save(folder/f'video-frame-{i:03}.jpg',quality=94)
    count+=1
assert count==450,count
for i in range(5):assert len({hashes[i*90+j] for j in [0,45,89]})==3
sheet=Image.new('RGB',(360*5,680),(8,20,18));d=ImageDraw.Draw(sheet)
for i,im in enumerate(selected.values()):
    sheet.paste(im.resize((360,640)),(360*i,0))
    d.text((360*i+12,652),f'{i*3:02}–{i*3+3:02}s / Camera {i+1}',fill='white')
sheet.save(folder/'five-shots.jpg',quality=92)
report={'durationSeconds':metadata['duration'],'fps':metadata['fps'],'resolution':metadata['size'],
        'decodedFrames':count,'movingShots':5,'cutsSeconds':[0,3,6,9,12],
        'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'bytes':path.stat().st_size,'samples':checks,
        'nativeReport':json.loads((folder/'status.json').read_text(encoding='utf-8'))}
(folder/'video-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ['samples','nativeReport']}))
