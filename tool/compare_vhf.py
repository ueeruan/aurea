import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

def main():
    ap=argparse.ArgumentParser();ap.add_argument('pass_name');args=ap.parse_args()
    folder=Path('build/render/vhf')/args.pass_name
    frames=sorted(folder.glob('frame-*.png'));rows=[]
    for f in frames:
        q=int(f.stem.split('-')[-1])
        a=np.asarray(Image.open(Path('build/vhf-study')/f'ref-{q:03d}.png').convert('RGB'),dtype=float)
        b=np.asarray(Image.open(f).convert('RGB'),dtype=float)
        rows.append({'frame':q,'mae':float(np.abs(a-b).mean()),'rmse':float(np.sqrt(np.square(a-b).mean()))})
    result={'frames':len(rows),'mae_0_255':float(np.mean([r['mae'] for r in rows])),
        'pixel_identical_frames':sum(r['mae']==0 for r in rows),'per_frame':rows}
    (folder/'comparison.json').write_text(json.dumps(result,indent=2))
    selected=[4,16,42,66,80,94,114,138,160,172,184,206,218,230]
    sheet=Image.new('RGB',(720,7*340),'#171717');draw=ImageDraw.Draw(sheet)
    for i,q in enumerate(selected):
        x=i%2*360;y=i//2*340
        for j,root in enumerate([Path('build/vhf-study'),folder]):
            f=root/(f'ref-{q:03d}.png' if j==0 else f'frame-{q:03d}.png')
            if not f.exists():continue
            sheet.paste(Image.open(f).resize((180,320)),(x+j*180,y+20))
            draw.text((x+j*180+4,y+3),f'{q:03d} '+('REF' if j==0 else 'AUREA'),fill='white')
    sheet.save(folder/'comparison.jpg',quality=94)
    print(json.dumps({k:v for k,v in result.items() if k!='per_frame'}))
    for a,b in zip([0,12,36,60,86,106,130,152,178,200,212,224],[12,36,60,86,106,130,152,178,200,212,224,234]):
        data=[r['mae'] for r in rows if a<=r['frame']<b]
        if data:print(f'{a:03d}-{b-1:03d}: {np.mean(data):.2f}')

if __name__=='__main__':main()
