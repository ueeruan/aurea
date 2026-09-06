"""Reference inspection: contact sheets and per-frame colour/change measures."""
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw
from reference_study import components


def main():
    folder = Path('build/dnyx-study-20260905')
    frames = sorted(folder.glob('ref-*.png'))
    metrics, previous = [], None
    for n, path in enumerate(frames):
        a = np.asarray(Image.open(path).convert('RGB'), dtype=float)
        metrics.append({
            'frame': n, 'time_seconds': n * 100 / 2997,
            'background_rgb': np.median(a[12:100, 100:400], axis=(0, 1)).tolist(),
            'change_mae': None if previous is None else float(np.abs(a-previous).mean()),
        })
        previous = a
    (folder / 'frame-analysis.json').write_text(json.dumps(metrics, indent=2))
    pixel_frames=[]
    for n in range(24):
        rgb=np.asarray(Image.open(frames[n]).convert('RGB'),dtype=float)
        r,g,b=rgb.transpose(2,0,1)
        masks=[(b-r>45)&(b>105)&(b-g>35),
               (g-r>30)&(b>115)&(g>85)&(b-g<=35)&(g<160),
               (g-r>40)&(g>=160)&(b>145)]
        pixels=[]
        for kind,mask in enumerate(masks):
            mask[:110]=False;mask[350:]=False;mask[:,:95]=False
            for group in components(mask):
                if sum(z-a for _,a,z in group)<90:continue
                x0=min(a for _,a,_ in group);x1=max(z for _,_,z in group)
                y0=min(y for y,_,_ in group);y1=max(y for y,_,_ in group)+1
                if x1-x0<8 or y1-y0<8:continue
                pixels.append([kind,(x0+x1)/2,(y0+y1)/2,x1-x0,y1-y0])
        pixel_frames.append(pixels)
    (folder/'pixel-geometry.json').write_text(json.dumps(pixel_frames))
    for start, end, step in [(0, 72, 3), (72, 132, 2), (132, 192, 2), (192, 240, 2), (240, 309, 3)]:
        ns = list(range(start, end, step))
        cols, size = 6, 192
        sheet = Image.new('RGB', (cols*size, ((len(ns)+cols-1)//cols)*(size+22)), '#202020')
        draw = ImageDraw.Draw(sheet)
        for i, n in enumerate(ns):
            x, y = i % cols * size, i // cols * (size+22)
            sheet.paste(Image.open(folder/f'ref-{n:03d}.png').resize((size, size)), (x, y+22))
            draw.text((x+5, y+4), f'{n:03d} / {n*100/2997:.3f}s', fill='white')
        sheet.save(folder/f'scene-{start:03d}.jpg', quality=92)
    print(f'Inspected {len(metrics)} frames, including original presentation times.')


if __name__ == '__main__':
    main()
