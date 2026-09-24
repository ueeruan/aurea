"""Check copied iOS resources against the current Android source of truth.

This checks resource identity, not visual or functional parity on devices.
"""
import hashlib
import plistlib
from pathlib import Path


def validate():
    root = Path(__file__).resolve().parents[4]
    app = root / 'engine/platform/ios/app'
    android = root / 'android/app/src/main'
    pairs = [(android / 'res/font/cupertino_icons.ttf', app / 'CupertinoIcons.ttf')]
    pairs.append((android / 'assets/previa_efeitos.jpg', app / 'previa_efeitos.jpg'))
    pairs += [(android / f'assets/presets/{name}.json', app / f'Resources/presets/{name}.json')
              for name in ('animacao', 'efeitos', 'curva', 'legenda')]
    for source, destination in pairs:
        if hashlib.sha256(source.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
            raise ValueError(f'iOS resource differs from Android: {destination.relative_to(root)}')
    info = plistlib.loads((app / 'Info.plist').read_bytes())
    if 'CupertinoIcons.ttf' not in info.get('UIAppFonts', []):
        raise ValueError('Official icon font is not registered in UIAppFonts')
    print(f'{len(pairs)} shared resources are byte-identical to Android; font is registered')


if __name__ == '__main__':
    validate()
