"""Compare frame N with frame N; no temporal alignment or source substitution."""
import argparse
import csv
import json
import math
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('source', type=Path)
    ap.add_argument('render', type=Path)
    args = ap.parse_args()
    rows = []
    for n in range(280):
        a = np.asarray(Image.open(args.source / f'ref-{n:03d}.png').convert('RGB'), dtype=float)
        b = np.asarray(Image.open(args.render / f'frame-{n:03d}.png').convert('RGB'), dtype=float)
        if a.shape != b.shape:
            raise ValueError(f'Frame {n}: unequal resolutions {a.shape}, {b.shape}')
        diff = a-b
        mse = np.mean(diff**2)
        rows.append({'frame': n, 'seconds': n/30, 'mae_rgb_0_255': float(np.abs(diff).mean()),
                     'psnr_db': 10*math.log10(255**2/mse) if mse else 100,
                     'identical': bool(np.array_equal(a,b))})
    with (args.render / 'comparison.csv').open('w', newline='', encoding='utf-8') as f:
        w = csv.DictWriter(f, fieldnames=rows[0].keys())
        w.writeheader(); w.writerows(rows)
    summary = {
        'frames_compared': len(rows),
        'pixel_identical_frames': sum(r['identical'] for r in rows),
        'mean_absolute_rgb_error': sum(r['mae_rgb_0_255'] for r in rows)/len(rows),
        'scenes': [{
            'name': name, 'start': a, 'end_exclusive': b,
            'mae': sum(r['mae_rgb_0_255'] for r in rows[a:b])/(b-a),
        } for name,a,b in [('house',0,44),('impact',44,70),('eye',70,140),('sword',140,208),('crowns',208,280)]]
    }
    (args.render / 'comparison.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    samples = [12,36,60,84,120,156,180,200,252,276]
    sheet = Image.new('RGB',(1440,690),'#16181d')
    draw = ImageDraw.Draw(sheet)
    for i,n in enumerate(samples):
        x,y=(i%5)*288,(i//5)*345
        for j,(folder,pattern) in enumerate([(args.source,'ref'),(args.render,'frame')]):
            im = Image.open(folder/f'{pattern}-{n:03d}.png').resize((144,256))
            sheet.paste(im,(x+j*144,y+28))
            draw.text((x+j*144+3,y+7),f'{n} '+('Original' if j==0 else 'AUREA'),fill='white')
        draw.text((x+3,y+293),f'MAE: {rows[n]["mae_rgb_0_255"]:.2f} / 255',fill='white')
    sheet.save(args.render/'comparison-contact.jpg')
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    main()
