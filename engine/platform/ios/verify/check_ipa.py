"""Validate the packaged iOS app, including the reserved Resources regression."""
import json
import hashlib
from pathlib import Path
import plistlib
import struct
import sys
import zipfile


def validate(path):
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        assert len(names) == len(set(names)), 'Duplicate archive entries'
        assert archive.testzip() is None, 'Corrupt ZIP entry'
        prefix = 'Payload/Aurea.app/'
        assert not any(n.startswith(prefix + 'Resources/') or n == prefix + 'Resources' for n in names), 'Forbidden iOS app root Resources directory'
        info = plistlib.loads(archive.read(prefix + 'Info.plist'))
        assert info['CFBundleIdentifier'] == 'com.aurea.aurea', 'Incorrect bundle ID'
        assert info['CFBundlePackageType'] == 'APPL', 'Not an app bundle'
        assert info['CFBundleVersion'].isdigit(), 'Invalid build version'
        binary = archive.read(prefix + info['CFBundleExecutable'])
        magic, cpu, _, kind = struct.unpack_from('<IIII', binary)
        assert (magic, cpu, kind) == (0xFEEDFACF, 0x0100000C, 2), 'Not an ARM64 executable'
        for name in ('animacao', 'efeitos', 'curva', 'legenda'):
            data = json.loads(archive.read(prefix + 'presets/' + name + '.json'))
            assert isinstance(data, list) and data, 'Missing preset catalog: ' + name
        assert prefix + 'Assets.car' in names, 'App assets missing'
        assert 'Fonts/Roboto-Regular.ttf' in info.get('UIAppFonts', []), 'Android reference UI font is not registered'
        reference_font = Path(__file__).resolve().parents[1] / 'app/Fonts/Roboto-Regular.ttf'
        assert hashlib.sha256(archive.read(prefix + 'Fonts/Roboto-Regular.ttf')).digest() == hashlib.sha256(reference_font.read_bytes()).digest(), 'UI font differs from reference Android system'
        assert 'CupertinoIcons.ttf' in info.get('UIAppFonts', []), 'Official icon font is not registered'
        official_font = Path(__file__).resolve().parents[4] / 'android/app/src/main/res/font/cupertino_icons.ttf'
        assert hashlib.sha256(archive.read(prefix + 'CupertinoIcons.ttf')).digest() == hashlib.sha256(official_font.read_bytes()).digest(), 'Icon font differs from Android'
        official_photo = Path(__file__).resolve().parents[4] / 'android/app/src/main/assets/previa_efeitos.jpg'
        assert hashlib.sha256(archive.read(prefix + 'previa_efeitos.jpg')).digest() == hashlib.sha256(official_photo.read_bytes()).digest(), 'Effect preview photo differs from Android'
        return info['CFBundleVersion']


if __name__ == '__main__':
    try:
        print('IPA bundle validated; build', validate(sys.argv[1]))
    except (AssertionError, KeyError, ValueError, zipfile.BadZipFile) as error:
        sys.exit('IPA ERROR: ' + str(error))
