"""Contact sheet of rendered test fixtures for visual QA (no source assets edited)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root = Path('build/qa/effects-v2')
files = [p for p in sorted(root.glob('*.png')) if p.name != 'contact-sheet.png']
sheet = Image.new('RGB', (5 * 260, ((len(files) + 4) // 5) * 158), '#171a20')
draw = ImageDraw.Draw(sheet)
font = ImageFont.truetype('C:/Windows/Fonts/arial.ttf', 15)
for index, path in enumerate(files):
    x, y = (index % 5) * 260, (index // 5) * 158
    sample = Image.open(path).convert('RGBA').resize((256, 128), Image.Resampling.NEAREST)
    checker = Image.new('RGB', sample.size, '#34383f')
    cells = ImageDraw.Draw(checker)
    for cy in range(0, 128, 16):
        for cx in range(0, 256, 16):
            if (cx // 16 + cy // 16) % 2:
                cells.rectangle((cx, cy, cx + 15, cy + 15), fill='#464b54')
    checker.paste(sample, (0, 0), sample)
    sheet.paste(checker, (x, y))
    draw.text((x + 4, y + 133), path.stem, font=font, fill='white')
sheet.save(root / 'contact-sheet.png')
print(root / 'contact-sheet.png')
