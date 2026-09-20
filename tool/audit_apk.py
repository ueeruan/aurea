"""Check the built APK's CRC, native code and bundled resources (not a device test)."""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('apk', type=Path)
args = parser.parse_args()
with zipfile.ZipFile(args.apk) as archive:
    assert archive.testzip() is None, 'Corrupt APK ZIP entry'
    names = archive.namelist()
    native = [name for name in names if name.endswith('/libapp.so')]
    assert 'lib/arm64-v8a/libapp.so' in native, 'No ARM64 executable'
    for name in native:
        code = archive.read(name)
        assert code[:4] == b'\x7fELF', name
        for marker in [b'Como usar o AUREA', b'vhf_neon_native_v1']:
            assert marker in code, f'Missing {marker!r} in {name}'
    shader = archive.read('assets/flutter_assets/shaders/effects_v2.frag')
    assert len(shader) > 1000, 'FX V2 shader missing/empty'
    assets = []
    for folder in ['assets/templates/vhf', 'assets/templates/dnyx']:
        assets.extend(path for path in Path(folder).iterdir() if path.is_file())
    for path in assets:
        assert archive.read('assets/flutter_assets/' + path.as_posix()) == path.read_bytes(), str(path)
    result = {
        'file': str(args.apk.resolve()),
        'bytes': args.apk.stat().st_size,
        'sha256': hashlib.sha256(args.apk.read_bytes()).hexdigest(),
        'zip_crc': 'valid',
        'architectures': [name.split('/')[1] for name in native],
        'fx_v2_shader': True,
        'offline_guide': True,
        'native_vhf_factory': True,
        'assets_verified': [path.as_posix() for path in assets],
        'note': 'Manifest and signing verified separately with aapt2 and apksigner.',
    }
    print(json.dumps(result, indent=2))
    (args.apk.parent / 'verification.json').write_text(json.dumps(result, indent=2))
