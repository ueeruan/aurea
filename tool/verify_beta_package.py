"""Verify a downloaded IPA or locally built APK before beta delivery."""
import argparse
import hashlib
import json
import plistlib
import struct
import subprocess
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('package', type=Path)
parser.add_argument('--build', required=True, type=int)
parser.add_argument('--abi', choices=['arm64-v8a', 'armeabi-v7a', 'x86_64'])
parser.add_argument('--android-tools', type=Path)
parser.add_argument('--certificate')
args = parser.parse_args()
path = args.package.resolve()
with path.open('rb') as package_stream:
    digest = hashlib.file_digest(package_stream, 'sha256').hexdigest()
result = {'file': str(path), 'bytes': path.stat().st_size, 'sha256': digest}
with zipfile.ZipFile(path) as z:
    assert z.testzip() is None, 'Corrupted ZIP entry'
    names = z.namelist()
    result['zip_crc'] = 'valid'
    if path.suffix.lower() == '.ipa':
        info = next(n for n in names if n.count('/') == 2 and n.endswith('.app/Info.plist'))
        root = info.removesuffix('Info.plist')
        p = plistlib.loads(z.read(info))
        assert str(p['CFBundleVersion']) == str(args.build), 'Wrong IPA build'
        assert p['CFBundleIdentifier'] == 'com.aurea.aurea'
        executable = z.read(root + p['CFBundleExecutable'])
        assert executable[:4] == b'\xcf\xfa\xed\xfe', 'Expected Mach-O 64-bit'
        assert struct.unpack_from('<I', executable, 4)[0] == 0x0100000c, 'Expected arm64'
        assert any(n.endswith('/App.framework/App') and z.getinfo(n).file_size > 100000 for n in names)
        result.update(build=p['CFBundleVersion'], version=p['CFBundleShortVersionString'],
                      bundle_id=p['CFBundleIdentifier'], minimum_os=p.get('MinimumOSVersion'),
                      architecture='arm64', unsigned=root + 'embedded.mobileprovision' not in names)
        prefix = next(n.removesuffix('AssetManifest.bin') for n in names if n.endswith('/flutter_assets/AssetManifest.bin'))
    else:
        assert args.abi and args.android_tools, 'APK needs ABI and Android build tools'
        assert f'lib/{args.abi}/libapp.so' in names, 'Wrong ABI'
        assert z.read(f'lib/{args.abi}/libapp.so')[:4] == b'\x7fELF'
        for lib in ['libflutter.so', 'libaurea_core.so']:
            assert f'lib/{args.abi}/{lib}' in names, f'Missing {lib}'
        badging = subprocess.check_output([str(args.android_tools/'aapt2.exe'), 'dump', 'badging', str(path)], text=True, encoding='utf-8')
        expected_code = {'armeabi-v7a': 1000, 'arm64-v8a': 2000, 'x86_64': 4000}[args.abi] + args.build
        assert "name='com.aurea.aurea'" in badging
        assert f"versionCode='{expected_code}'" in badging, 'Wrong APK build'
        assert 'application-debuggable' not in badging, 'Debug APK'
        cert = subprocess.check_output([str(args.android_tools/'apksigner.bat'), 'verify', '--print-certs', str(path)], text=True)
        if args.certificate:
            assert f'certificate SHA-256 digest: {args.certificate}' in cert, 'Signing certificate changed'
        result.update(build=args.build, version_code=expected_code, architecture=args.abi,
                      release=True, signing_verified=True, same_certificate=bool(args.certificate))
        prefix = 'assets/flutter_assets/'
    for shader in ['effects_v2.frag', 'blend.frag']:
        assert z.getinfo(prefix+'shaders/'+shader).file_size > 1000, f'Missing shader {shader}'
    for asset in ['assets/templates/notes.jpg', 'assets/templates/dnyx/gallery-left.png']:
        assert z.read(prefix+asset) == Path(asset).read_bytes(), f'Mismatched asset {asset}'
    result['shaders_and_sample_assets'] = 'valid'
report = path.with_suffix(path.suffix+'.verification.json')
report.write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps(result, indent=2))
