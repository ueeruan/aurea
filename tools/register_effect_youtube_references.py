"""Register observed public YouTube references; no third-party assets in the app."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[1]
folder = root / 'build/reference/requested-effects/references'
path = folder / 'catalog.json'
catalog = json.loads(path.read_text(encoding='utf-8'))
videos = [
    ('My_3x8yMDQY', 's_wipeflux mask slide tutorial on AFTER EFFECTS!', 'inside the mind of dylan', 251, 'Search title/channel verified; watch page opened. Full temporal inspection pending.'),
    ('oCYSc9KtIrw', 'S_WipePlasma | After Effects Tutorial', 'nikvfx', 105, 'Frame at 1:02 inspected: keyframes of Wipe Percent 0 to 100. Automated scene description is an estimate.'),
    ('qelbO7sJQy0', 'How To Use Expressions || Cool Warp Effect With S_Distort Tutorial !', 'ecspression', 99, 'Watch page and introductory frame inspected; parameter measurement pending.'),
    ('yXjI7iGwhpU', 'Pixel Encoder for After Effects and Premiere Pro', 'aescripts + aeplugins', 31, 'Official channel verified; colored vertical pattern at 0:07 inspected. Automated scene description is an estimate.'),
    ('Jh632SUXG98', 'Quick Tip: Using Curves View in Video Glitch', 'Boris FX', 174, 'Official channel, title, duration and description verified; parameter/temporal inspection pending.'),
    ('iArYISJphkc', 'PixDither plugin After effect| pixdither افتر افكت بلوقن', 'SAIBA', 128, 'Limited reference: primarily installation tutorial. AE application at 1:44; not a numerical or full visual benchmark.'),
    ('FLMdpxrKqlw', 'Displacer Pro - Easy And Fast Displacement Maps - After Effects FREE Plugin', 'Plugin Everything', 26, 'Official author/channel verified. Candidate for ambiguous Displace; no user confirmation of product identity.'),
    ('9zSakRsTH78', 'Creative Text Animation Tutorial in After Effects using Trapcode Starglow', 'Kreativ Tuts', 468, 'Title/channel and intro/result sampled. Multiple effects in stack: not an isolated Starglow benchmark.'),
]
for ref, (video_id, title, channel, seconds, inspected) in zip(catalog['references'], videos):
    ref['youtube'] = {'url': f'https://www.youtube.com/watch?v={video_id}', 'title': title, 'channel': channel, 'duration_seconds': seconds, 'inspection': inspected, 'accessed': '2026-10-09'}
catalog['claim'] = 'Official documentation plus public YouTube references. Samples inspected; no equivalent original renders or pixel-identical validation.'
path.write_text(json.dumps(catalog, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('Registered eight YouTube reference candidates, with individual inspection limits.')
