"""Compare exact reference/render frame indices; no temporal realignment."""
import argparse
import csv
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ap=argparse.ArgumentParser()
ap.add_argument('render',type=Path)
args=ap.parse_args()
source=Path('build/dnyx-study-20260905')
rows=[]
for path in sorted(args.render.glob('frame-*.png')):
    q=int(path.stem.split('-')[-1])
    ref=np.asarray(Image.open(source/f'ref-{q:03d}.png').convert('RGB'),dtype=float)
    made=np.asarray(Image.open(path).convert('RGB'),dtype=float)
    assert ref.shape==made.shape==(576,576,3)
    difference=np.abs(made-ref)
    mask=np.ones((576,576),dtype=bool)
    # Deliberate removals: moving watermark, and changed end credit.
    mask[248:310,:92]=False
    mask[515:,480:]=False
    if q>=273:
        mask[180:400,:]=False
    rows.append({'frame':q,'time':q*100/2997,'mae_all':float(difference.mean()),
                 'mae_outside_requested_edits':float(difference[mask].mean()),
                 'pixel_identical':bool(np.array_equal(ref,made))})
with (args.render/'comparison.csv').open('w',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
report={'frames_compared':len(rows),'pixel_identical_frames':sum(r['pixel_identical'] for r in rows),
        'mae_all':float(np.mean([r['mae_all'] for r in rows])),
        'mae_outside_requested_edits':float(np.mean([r['mae_outside_requested_edits'] for r in rows])),
        'note':'RGB error on 0-255 scale, NOT a percentage of similarity. Watermark/credit are intentional changes.'}
(args.render/'comparison.json').write_text(json.dumps(report,indent=2))
selected=[0,9,18,36,60,72,84,96,107,120,132,144,153,180,198,212,228,240,264,280,308]
sheet=Image.new('RGB',(1152,7*216),'#181818');draw=ImageDraw.Draw(sheet)
for i,q in enumerate(selected):
    x=i%3*384;y=i//3*216
    draw.text((x+5,y+4),f'{q:03d} original | AUREA',fill='white')
    for offset,path in [(0,source/f'ref-{q:03d}.png'),(192,args.render/f'frame-{q:03d}.png')]:
        if path.exists():sheet.paste(Image.open(path).resize((192,192)),(x+offset,y+24))
sheet.save(args.render/'comparison-contact.jpg',quality=94)
print(json.dumps(report,indent=2))
