"""Verify every embedded shader is a linked iOS metallib before packaging."""
from pathlib import Path
import struct
import sys

root = Path(sys.argv[1])
blobs = sorted(root.rglob('*.mslblob'))
# Wipe, optical effects and sampled box/directional blur add three passes.
if len(blobs) != 70:
    raise SystemExit(f'Expected 70 Metal shaders, found {len(blobs)}')

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
