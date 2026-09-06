"""Read-only IPA integrity and native-bundle verification; no signing changes."""
import argparse
import hashlib
import json
import plistlib
import struct
import zipfile
from pathlib import Path

def main():
    ap=argparse.ArgumentParser();ap.add_argument('ipa',type=Path);args=ap.parse_args()
    with zipfile.ZipFile(args.ipa) as z:
        assert z.testzip() is None, 'Corrupted ZIP entry'
        names=z.namelist()
        plist_name=next(n for n in names if n.count('/')==2 and n.endswith('.app/Info.plist'))
        p=plistlib.loads(z.read(plist_name));root=plist_name.removesuffix('Info.plist')
        assert str(p['CFBundleVersion'])=='35'
        assert p['CFBundleIdentifier']=='com.aurea.aurea'
        executable=z.read(root+p['CFBundleExecutable'])
        assert executable[:4]==b'\xcf\xfa\xed\xfe', 'Expected Mach-O 64-bit'
        assert struct.unpack_from('<I',executable,4)[0]==0x0100000c, 'Expected arm64'
        matched=[]
        for folder in ['assets/templates/vhf','assets/templates/dnyx']:
            for f in sorted(Path(folder).iterdir()):
                if not f.is_file():continue
                key=next(n for n in names if n.endswith('/flutter_assets/'+f.as_posix()))
                expected=hashlib.sha256(f.read_bytes()).hexdigest()
                assert hashlib.sha256(z.read(key)).hexdigest()==expected, f'Mismatched {f}'
                matched.append(f.as_posix())
        aot=next(n for n in names if n.endswith('/App.framework/App'))
        binary=z.read(aot)
        assert b'vhf_neon_native_v1' in binary, 'VHF project factory not found in AOT'
        assert b'Animar cores' in binary, 'Animated-gradient UI not found in AOT'
        assert b'abyss_cinematic_template' in binary, 'Bundled ABISMO installation missing'
        assert b'Como usar o AUREA' in binary, 'Offline guide missing from AOT'
        shader_key=next(n for n in names if n.endswith('/flutter_assets/shaders/effects_v2.frag'))
        assert len(z.read(shader_key)) > 1000, 'FX V2 shader missing/empty'
        preview_key=next(n for n in names if n.endswith('/flutter_assets/assets/templates/abyss.jpg'))
        assert z.read(preview_key)==Path('assets/templates/abyss.jpg').read_bytes()
        result={'file':str(args.ipa.resolve()),'bytes':args.ipa.stat().st_size,
            'sha256':hashlib.sha256(args.ipa.read_bytes()).hexdigest(),
            'version':p['CFBundleShortVersionString'],'build':p['CFBundleVersion'],
            'minimum_os':p.get('MinimumOSVersion'),'bundle_id':p['CFBundleIdentifier'],
            'architecture':'arm64','zip_crc':'valid','bundled_assets_verified':matched,
            'native_vhf_factory':True,'animated_gradient_ui':True,
            'fx_v2_shader':True,'offline_guide':True,'bundled_abyss':True,
            'has_provision_profile':root+'embedded.mobileprovision' in names}
        print(json.dumps(result,indent=2))
        (args.ipa.parent/'verification.json').write_text(json.dumps(result,indent=2))

if __name__=='__main__':main()
