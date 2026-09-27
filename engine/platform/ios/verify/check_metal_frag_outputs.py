"""Metal fragment-output contract (engine/gpu/metal/msl_glue.md section 5).

Metal refuses a render pipeline whose fragment function writes a color
attachment the descriptor does not have (pixelFormat Invalid); Vulkan and GLES
drop that write. The engine's 3D shaders declare `layout(location = 1) out`
(MRT: 2D display + HDR scene) and are ALSO bound in single-target pipelines
(2D particles, planes of a group without HDR, the direct sky pass). Without the
`fs_main_c0` variant those draws are silently skipped on iOS.

Static audit (always, no Mac needed):
  * at least one shader declares location >= 1 (the rule has a subject);
  * the host translator emits the masked variant (`enable_frag_output_mask`,
    entry `fs_main_c0`, file `.c0.metal`);
  * the build links that file into the same metallib (compile_metal.cmake and
    AureaShaders.cmake reference it);
  * the backend looks the variant up and binds it when there is no second
    color attachment.

End-to-end (when both tools are given, e.g. on the host that built
engine/tools/metal-shaders): every shader with location >= 1 must translate
into a `.c0.metal` that has `fs_main_c0`, one `[[color(0)]]` and no
`[[color(1)]]`, and the main translation keeps `[[color(1)]]`; a shader
without location >= 1 must NOT produce the variant.

    python check_metal_frag_outputs.py [--glslc PATH --tool PATH]
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
SHADERS = ROOT / 'engine/shaders'
OUT_RE = re.compile(r'layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*out\b')


def mrt_fragment_shaders():
    """Fragment shaders (with their includes) declaring an output at location >= 1."""
    found = []
    for frag in sorted(SHADERS.rglob('*.frag')):
        text = frag.read_text(encoding='utf-8')
        # Outputs may live in an included .glsl (yuv_convert.glsl style).
        for inc in re.findall(r'#include\s+"([^"]+)"', text):
            path = (frag.parent / inc).resolve()
            if path.is_file():
                text += '\n' + path.read_text(encoding='utf-8')
        if any(int(loc) >= 1 for loc in OUT_RE.findall(text)):
            found.append(frag)
    return found


def static_audit(mrt):
    def must(path, needle, why):
        if needle not in (ROOT / path).read_text(encoding='utf-8'):
            raise ValueError(f'{path}: missing "{needle}" ({why})')

    if not mrt:
        raise ValueError('no fragment shader declares location >= 1; the MRT contract lost its subject')
    tool = 'engine/tools/metal-shaders/main.cpp'
    must(tool, 'enable_frag_output_mask', 'translator must mask outputs for the single-target variant')
    must(tool, '"fs_main_c0"', 'translator must emit the fs_main_c0 entry point')
    must(tool, '.c0.metal', 'translator must write the variant to <out>.c0.metal')
    script = 'engine/tools/metal-shaders/compile_metal.cmake'
    must(script, '.c0.metal', 'build must compile the variant')
    must(script, 'metallib ${_airs}', 'build must link the variant into the same metallib')
    must('engine/cmake/AureaShaders.cmake', 'compile_metal.cmake', 'iOS shader build must go through the script')
    backend = 'engine/gpu/metal/MetalResources.mm'
    must(backend, 'newFunctionWithName:@"fs_main_c0"', 'backend must look the variant up')
    must(backend, '(!mrt && fs->functionColor0) ? fs->functionColor0 : fs->function',
         'backend must bind the variant in pipelines without a second color attachment')
    must('engine/gpu/metal/MetalInternal.hpp', 'functionColor0', 'ShaderObject must carry the variant')


def end_to_end(glslc, tool, mrt):
    plain = SHADERS / 'composite/output.frag'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for frag in mrt + [plain]:
            name = frag.relative_to(SHADERS).as_posix().replace('/', '_')
            spv = work / (name + '.spv')
            raw = work / (name + '.msl')
            subprocess.run([glslc, '--target-env=vulkan1.0', '-O', '-Werror', '-I', str(SHADERS),
                            '-o', str(spv), str(frag)], check=True)
            subprocess.run([tool, str(spv), str(raw)], check=True)
            main = (work / (name + '.msl.metal')).read_text(encoding='utf-8')
            c0 = work / (name + '.msl.c0.metal')
            if frag == plain:
                if c0.exists():
                    raise ValueError(f'{frag.name}: no location >= 1, yet a .c0.metal variant was written')
                continue
            if '[[color(1)]]' not in main or 'fs_main(' not in main:
                raise ValueError(f'{frag.name}: main translation lost color(1) or fs_main')
            if not c0.exists():
                raise ValueError(f'{frag.name}: declares location >= 1 but no .c0.metal variant')
            text = c0.read_text(encoding='utf-8')
            if 'fs_main_c0(' not in text:
                raise ValueError(f'{frag.name}: variant lacks the fs_main_c0 entry point')
            if text.count('[[color(0)]]') != 1 or '[[color(1)]]' in text:
                raise ValueError(f'{frag.name}: variant must write color(0) only')
    print(f'{len(mrt)} MRT fragment shaders translate with an fs_main_c0 variant; {plain.name} has none')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--glslc')
    parser.add_argument('--tool', help='aurea-metal-compiler built for the host')
    args = parser.parse_args()
    mrt = mrt_fragment_shaders()
    static_audit(mrt)
    print(f'{len(mrt)} fragment shaders declare location >= 1; translator, build and backend carry fs_main_c0')
    if bool(args.glslc) != bool(args.tool):
        parser.error('--glslc and --tool go together')
    if args.glslc:
        # Absolute paths: CreateProcess on Windows does not resolve a relative
        # executable path the way a shell does.
        end_to_end(str(Path(args.glslc).resolve()), str(Path(args.tool).resolve()), mrt)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f'check_metal_frag_outputs: {error}', file=sys.stderr)
        sys.exit(1)
