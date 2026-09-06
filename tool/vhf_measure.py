"""Measure palettes and isolated light orbs, not replacement picture frames."""
import json
from pathlib import Path
import numpy as np
from PIL import Image
from reference_study import components

ROOT = Path('build/vhf-study')

def main():
    palettes, orbs = [], {}
    previous = None
    ranges = [(60,72),(86,106),(152,166),(168,178),(212,224),(226,234)]
    for q in range(234):
        a = np.array(Image.open(ROOT/f'ref-{q:03d}.png').convert('RGB'))
        # Background ramps: keep geometric objects independent from these colors.
        column = 710 if 86 <= q < 106 or 152 <= q < 178 else 5
        vertical = [a[min(1279, round(y)), column].tolist() for y in np.linspace(0,1279,17)]
        horizontal = [a[80, min(719, round(x))].tolist() for x in np.linspace(0,719,17)]
        palettes.append({'vertical': vertical, 'horizontal': horizontal})
        if any(lo <= q < hi for lo,hi in ranges):
            mask = (a[:,:,:3].min(axis=2)>220)
            if q < 72: mask[1000:]=False
            if 86 <= q <106: mask[620:]=False
            if 152<=q<178: mask[940:]=False
            if q>=212: mask[650:]=False
            cs=[]
            for c in components(mask):
                area=sum(b-a for _,a,b in c)
                x0=min(a for _,a,_ in c);x1=max(b for _,_,b in c)
                y0=min(y for y,_,_ in c);y1=max(y for y,_,_ in c)+1
                w,h=x1-x0,y1-y0
                if area<120 or w>360 or h>600:continue
                if area/(w*h)<.50:continue
                cs.append((area,[(x0+x1)/2,(y0+y1)/2,w,h]))
            if cs:
                orbs[str(q)]=max(cs)[1]
                if 212 <= q < 224:
                    x,y,w,h=orbs[str(q)]
                    orbs[str(q)]=[x,y-h/2+w/2,w,w]
    orbs['165']=[360.0,550.0,80.0,300.0]
    orbs['177']=[820.0,500.0,460.0,150.0]
    data={'palettes':palettes, 'orbs':orbs}
    (ROOT/'measurements.json').write_text(json.dumps(data))
    out=Path('lib/src/features/projects/domain/vhf_motion_samples.dart')
    def color(c): return f'0xff{c[0]:02x}{c[1]:02x}{c[2]:02x}'
    lines=['// Measured colors and orb bounds; no source-frame imagery.','const vhfVerticalPalettes = <List<int>>[']
    lines += ['  ['+', '.join(map(color,p['vertical']))+'],' for p in palettes]
    lines += ['];','const vhfHorizontalPalettes = <List<int>>[']
    lines += ['  ['+', '.join(map(color,p['horizontal']))+'],' for p in palettes]
    lines += ['];','const vhfOrbBounds = <int, List<double>>{']
    lines += [f'  {q}: {v},' for q,v in orbs.items()]
    lines += ['};']
    # Emitted as generated measurement data, not handwritten app source.
    out.write_text('\n'.join(lines)+'\n')
    print(f'Measured 234 palettes and {len(orbs)} orb silhouettes.')

if __name__=='__main__':main()
