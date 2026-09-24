"""Port the exact Compose 1.7.8 material vector paths used by the Android UI.

Source jars: dl.google.com/dl/android/maven2/androidx/compose/material/
material-icons-{core,extended}/1.7.8/*-sources.jar (Apache-2.0).
No icon substitutions: unsupported path commands fail generation.
"""
import hashlib
import json
import re
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sources = {}
for kind in ('core', 'extended'):
    with zipfile.ZipFile(ROOT / f'build/material-icons-{kind}-1.7.8-sources.jar') as jar:
        for name in jar.namelist():
            if '/icons/' in name and name.endswith('.kt'):
                key = name.split('/icons/')[1][:-3].replace('/', '.')
                sources[key] = jar.read(name).decode('utf-8')

used = {'filled.ChevronLeft', 'rounded.ChevronLeft', 'rounded.OpenWith', 'rounded.AspectRatio',
        'automirrored.rounded.RotateRight', 'rounded.Opacity', 'rounded.FilterCenterFocus',
        'rounded.BlurOn', 'rounded.Link', 'rounded.LinkOff', 'filled.MoreVert', 'filled.MoreHoriz',
        'filled.Add', 'automirrored.filled.Logout', 'automirrored.rounded.FormatAlignLeft',
        'automirrored.rounded.FormatAlignRight', 'rounded.Stairs'}
for file in (ROOT / 'android/app/src/main/java').rglob('*.kt'):
    used.update(re.findall(r'import androidx\.compose\.material\.icons\.([\w.]+)', file.read_text(encoding='utf-8')))

commands = {'moveTo': 'M', 'lineTo': 'L', 'horizontalLineTo': 'H', 'verticalLineTo': 'V',
            'curveTo': 'C', 'reflectiveCurveTo': 'S', 'quadTo': 'Q', 'reflectiveQuadTo': 'T', 'arcTo': 'A'}
assets = ROOT / 'engine/platform/ios/app/Assets.xcassets'
manifest = {}
for key in sorted(k for k in used if "." in k):
    if key not in sources:
        raise ValueError(f'Missing official source: {key}')
    source = sources[key]
    paths = []
    for attrs, body in re.findall(r'materialPath(?:\((.*?)\))?\s*\{(.*?)\}', source, re.S):
        parts = []
        for operation, args in re.findall(r'(\w+)\((.*?)\)', body, re.S):
            if operation == 'close':
                parts.append('Z')
                continue
            base = operation.removesuffix('Relative')
            if base not in commands:
                raise ValueError(f'{key}: unsupported {operation}')
            command = commands[base].lower() if operation.endswith('Relative') else commands[base]
            numbers = args.replace('true', '1').replace('false', '0').replace('f', '')
            parts.append(command + ' '.join(x.strip() for x in numbers.split(',')))
        alpha = re.search(r'fillAlpha\s*=\s*([.\d]+)', attrs)
        opacity = f' opacity="{alpha[1]}"' if alpha else ''
        paths.append(f'<path d="{" ".join(parts)}"{opacity}/>')
    if not paths:
        raise ValueError(f'{key}: no paths')
    name = 'Material-' + key.replace('.', '-')
    dest = assets / (name + '.imageset')
    dest.mkdir(parents=True, exist_ok=True)
    svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">' + ''.join(paths) + '</svg>\n'
    (dest / 'icon.svg').write_text(svg, encoding='utf-8')
    (dest / 'Contents.json').write_text(json.dumps({'images': [{'filename': 'icon.svg', 'idiom': 'universal'}],
        'info': {'author': 'xcode', 'version': 1}, 'properties': {'preserves-vector-representation': True,
        'template-rendering-intent': 'template'}}, indent=2), encoding='utf-8')
    manifest[key] = {'sourceSHA256': hashlib.sha256(source.encode()).hexdigest(), 'asset': name}
(ROOT / 'engine/platform/ios/app/Resources/material-icons-provenance.json').write_text(
    json.dumps({'version': '1.7.8', 'license': 'Apache-2.0', 'icons': manifest}, indent=2), encoding='utf-8')
print(f'{len(manifest)} exact Material icon assets generated')
