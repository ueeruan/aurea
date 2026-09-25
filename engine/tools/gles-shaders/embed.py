"""Embed translated ESSL with the exact SPIR-V fingerprint it implements."""
import argparse
import re
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("inputs", type=Path, nargs="+")
args = parser.parse_args()
parts = ['#include "aurea/core/Types.hpp"\nnamespace aurea::gles {\nnamespace {\n',
         'struct Entry { u64 fingerprint; const char* source; };\nconst Entry entries[] = {\n']
for path in args.inputs:
    source = path.read_text(encoding="utf-8")
    match = re.match(r"// AUREA_SPIRV_FNV64 ([0-9a-f]+)\n", source)
    if not match or ')AUREAGLES"' in source:
        raise ValueError(f"invalid generated ES shader: {path}")
    parts.append('{0x' + match[1] + 'ull, R"AUREAGLES(' + source + ')AUREAGLES"},\n')
parts.append('};\n}\nconst char* shader_source(const u32* words, usize bytes) noexcept {\n'
             'if (!words || !bytes) return nullptr;\n'
             'u64 hash = 14695981039346656037ull;\n'
             'const auto* data = reinterpret_cast<const unsigned char*>(words);\n'
             'for (usize i = 0; i < bytes; ++i) { hash ^= data[i]; hash *= 1099511628211ull; }\n'
             'for (const auto& entry : entries) if (entry.fingerprint == hash) return entry.source;\n'
             'return nullptr;\n}\n}\n')
args.output.write_text(''.join(parts), encoding="utf-8")
