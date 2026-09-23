"""Replace an AUREAMSL source payload with Apple's precompiled metallib."""
from pathlib import Path
import struct
import sys

source, library, output = map(Path, sys.argv[1:4])
raw = source.read_bytes()
magic, version, flags, source_bytes, gx, gy, gz = struct.unpack_from('<8s6I', raw)
if magic != b'AUREAMSL' or version != 1 or flags != 0 or source_bytes == 0:
    raise SystemExit(f'invalid MSL source header: {source}')
if raw[32:32 + source_bytes].decode('utf-8').strip() == '':
    raise SystemExit(f'empty MSL source: {source}')
metal = library.read_bytes()
if len(metal) < 32:
    raise SystemExit(f'empty metallib: {library}')
header = struct.pack('<8s6I', magic, version, 1, len(metal), gx, gy, gz)
pad = b'\0' * ((-len(metal)) % 4)
output.write_bytes(header + metal + pad)
