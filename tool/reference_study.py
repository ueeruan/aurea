"""Measure the supplied source, never the older AUREA template.

Outputs are analysis data only. The editable reconstruction draws its own
geometry; source pixels are not embedded as replacement animation frames.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def components(mask):
    # Run-length connected components avoids optional computer-vision packages.
    parent, runs, previous = [], [], []
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    for y, row in enumerate(mask):
        changes = np.diff(np.r_[False, row, False].astype(np.int8))
        current = []
        for x0, x1 in zip(np.where(changes == 1)[0], np.where(changes == -1)[0]):
            i = len(parent)
            parent.append(i)
            runs.append((y, int(x0), int(x1)))
            for a, b, j in previous:
                if a <= x1 and b >= x0:
                    parent[root(j)] = root(i)
            current.append((x0, x1, i))
        previous = current
    groups = {}
    for i, (y, a, b) in enumerate(runs):
        groups.setdefault(root(i), []).append((y, a, b))
    return sorted(groups.values(), key=lambda g: sum(b-a for _, a, b in g), reverse=True)


def points(group):
    return np.array([(x, y) for y, a, b in group for x in range(a, b)], dtype=float)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('frames', type=Path)
    args = ap.parse_args()
    results = []
    for f in sorted(args.frames.glob('ref-*.png')):
        n = int(f.stem.split('-')[1])
        rgb = np.array(Image.open(f).convert('RGB')).astype(float)
        r, g, b = rgb.transpose(2, 0, 1)
        entry = {'frame': n}
        if n < 70:
            mask = (g-r > 45) & (g > 115) & (g > b*1.08)
            mask[:570] = False
            mask[815:] = False
            groups = components(mask)
            if groups:
                p = points(groups[0])
                entry['house'] = [*p.min(axis=0), *p.max(axis=0)]
        if n < 140:
            mask = (r > 220) & (g > 214) & (b > 214)
            mask[1000:] = False
            groups = components(mask)
            if groups:
                p = points(groups[0])
                center = p.mean(axis=0)
                z = (p[:, 0]-center[0]) + 1j*(p[:, 1]-center[1])
                angle = np.angle(np.mean(z**4))/4
                radius = np.quantile(np.abs(z), .997)
                entry['spark'] = [*center, float(radius), float(angle*180/np.pi)]
        entry['background'] = [rgb[y, 15].astype(int).tolist() for y in [0,400,800,820,900,980,1060,1140,1220,1277]]
        results.append(entry)
    (args.frames/'measurements.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
    for start, end, step in [(0,70,6),(70,140,6),(140,208,6),(208,280,6)]:
        sheet = Image.new('RGB', (1200,690), '#222222')
        draw = ImageDraw.Draw(sheet)
        for i,n in enumerate(range(start,end,step)):
            im = Image.open(args.frames/f'ref-{n:03d}.png').resize((200,355))
            sheet.paste(im,(i%6*200,i//6*345))
            draw.text((i%6*200+5,i//6*345+5),str(n),fill='yellow')
        sheet.save(args.frames/f'scene-{start}.jpg')
    print(json.dumps([e for e in results if e['frame']%6==0 and e['frame']<140], indent=2))


if __name__ == '__main__':
    main()
