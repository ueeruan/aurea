"""Verify every embedded shader is a linked iOS metallib before packaging."""
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
blobs = sorted(root.rglob('*.mslblob'))
# Match CMake's shader catalog, including paths, so a newly added shader is
# required without a manually maintained count masking missing/stale outputs.
source_root = Path(__file__).resolve().parents[2] / 'shaders'
expected = {
    source.relative_to(source_root).as_posix() + '.spv.mslblob'
    for extension in ('*.vert', '*.frag', '*.comp')
    for source in source_root.rglob(extension)
}
actual = {blob.relative_to(root).as_posix() for blob in blobs}
if not expected or actual != expected:
    missing = ', '.join(sorted(expected - actual)) or 'none'
    unexpected = ', '.join(sorted(actual - expected)) or 'none'
    raise SystemExit(f'Metal shader catalog mismatch: missing {missing}; unexpected {unexpected}')

failures = 0
for blob in blobs:
    data = blob.read_bytes()
    try:
        magic, version, flags, size, gx, gy, gz = struct.unpack_from('<8s6I', data)
        valid = (magic == b'AUREAMSL' and version == flags == 1 and size >= 32
                 and size <= len(data) - 32 and blob.with_suffix('.metallib').is_file())
    except struct.error:
        valid = False
    if not valid:
        failures += 1
        print(f'Invalid linked Metal shader: {blob}', flush=True)
print(f'Apple Metal compiler/linker: {len(blobs)-failures}/{len(blobs)} shaders passed')
raise SystemExit(bool(failures))
