"""Check copied iOS resources against the current Android source of truth.

This checks resource identity, not visual or functional parity on devices.
"""
import hashlib
import json
import plistlib
from pathlib import Path


def validate():
    root = Path(__file__).resolve().parents[4]
    app = root / 'engine/platform/ios/app'
    android = root / 'android/app/src/main'
    pairs = [(android / 'res/font/cupertino_icons.ttf', app / 'CupertinoIcons.ttf')]
    # Reserva árabe do motor: a mesma Noto Naskh Arabic (OFL) nos dois apps.
    pairs += [(root / f'engine/assets/fonts/{name}', app / f'Fonts/{name}')
              for name in ('NotoNaskhArabic-Regular.ttf', 'LICENSE-NotoNaskhArabic.txt')]
    pairs += [(android / f'assets/{name}', app / f'Fonts/{name}')
              for name in ('NotoSansJP-Regular.otf', 'LICENSE-NotoSansJP.txt')]
    pairs.append((android / 'assets/previa_efeitos.jpg', app / 'previa_efeitos.jpg'))
    pairs += [(android / f'assets/presets/{name}.json', app / f'Resources/presets/{name}.json')
              for name in ('animacao', 'efeitos', 'curva', 'legenda')]
    pairs.append((android / 'assets/editor_commands.json', app / 'Resources/editor_commands.json'))
    for source, destination in pairs:
        if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
            raise ValueError(f'iOS resource differs from Android: {destination.relative_to(root)}')
    jp_metadata = [json.loads(path.read_text(encoding='utf-8')) for path in
                   (android / 'assets/provenance-NotoSansJP.json', app / 'Fonts/provenance-NotoSansJP.json')]
    for key in ('source', 'version', 'license', 'licenseFile', 'sha256'):
        if jp_metadata[0][key] != jp_metadata[1][key]:
            raise ValueError(f'Japanese font provenance differs: {key}')
    if hashlib.sha256((app / 'Fonts/NotoSansJP-Regular.otf').read_bytes()).hexdigest() != jp_metadata[0]['sha256']:
        raise ValueError('Japanese font does not match its provenance checksum')
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    if 'CupertinoIcons.ttf' not in info.get('UIAppFonts', []):
        raise ValueError('Official icon font is not registered in UIAppFonts')
    print(f'{len(pairs)} shared resources are byte-identical to Android; font is registered')


if __name__ == '__main__':
    validate()
