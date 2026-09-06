"""Decode every VHF source frame and record its presentation time and changes."""
import json
import subprocess
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

SOURCE = Path(r'C:\Users\SnyX\Downloads\Video\Vhfdigital_pindown.io_1788585393.mp4')
TOOLS = Path(r'C:\Users\SnyX\Downloads\Motion 2.0\tools\ffmpeg\bin')
OUT = Path('build/vhf-study')

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    info = json.loads(subprocess.check_output([str(TOOLS/'ffprobe.exe'), '-v', 'error',
        '-select_streams', 'v:0', '-show_frames', '-show_streams', '-of', 'json', str(SOURCE)]))
    (OUT/'source-timing.json').write_text(json.dumps(info, indent=2))
    subprocess.run([str(TOOLS/'ffmpeg.exe'), '-v', 'error', '-y', '-i', str(SOURCE),
        '-fps_mode', 'passthrough', '-start_number', '0', str(OUT/'ref-%03d.png')], check=True)
    files = sorted(OUT.glob('ref-*.png'))
    previous = None
    rows = []
    for q, f in enumerate(files):
        a = np.asarray(Image.open(f).convert('RGB'), dtype=float)
        rows.append({'frame': q, 'pts': info['frames'][q]['best_effort_timestamp_time'],
            'mean_rgb': a.mean(axis=(0,1)).tolist(),
            'change_mae': None if previous is None else float(np.abs(a-previous).mean())})
        previous = a
    (OUT/'frame-analysis.json').write_text(json.dumps(rows, indent=2))
    for start in range(0, len(files), 60):
        ns = list(range(start, min(len(files), start+60), 2))
        sheet = Image.new('RGB', (6*180, 5*340), '#181818')
        draw = ImageDraw.Draw(sheet)
        for i,q in enumerate(ns):
            x,y=i%6*180,i//6*340
            sheet.paste(Image.open(files[q]).resize((180,320)), (x,y+20))
            draw.text((x+4,y+3), f'{q:03d} | {q*1000/24209:.3f}s', fill='white')
        sheet.save(OUT/f'contact-{start:03d}.jpg', quality=94)
    print(f'All {len(files)} frames decoded, measured and indexed.')

if __name__ == '__main__': main()
