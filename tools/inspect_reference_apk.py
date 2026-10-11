"""Static reference inventory. Never executes or imports code from the APK.

Unpacked files and raw metadata stay in an ignored build directory. Product
code must be authored independently; this inventory is evidence, not a port.
"""
from __future__ import annotations

import argparse
import collections
import hashlib
import json
from pathlib import Path, PurePosixPath
import struct
import xml.etree.ElementTree as ET
import zipfile


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def uleb(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    for shift in range(0, 35, 7):
        byte = data[offset]
        offset += 1
        value |= (byte & 127) << shift
        if byte < 128:
            return value, offset
    raise ValueError("Invalid ULEB128")


def dex_index(data: bytes) -> dict:
    if data[:4] != b"dex\n":
        raise ValueError("Not a standard DEX")
    count, start = struct.unpack_from("<II", data, 56)
    strings = []
    for index in range(count):
        offset = u32(data, start + 4 * index)
        _, offset = uleb(data, offset)
        strings.append(data[offset:data.index(b"\0", offset)].decode("utf-8", errors="replace"))
    count, start = struct.unpack_from("<II", data, 64)
    types = [strings[u32(data, start + 4 * i)] for i in range(count)]
    count, start = struct.unpack_from("<II", data, 96)
    classes = [types[u32(data, start + 32 * i)] for i in range(count)]
    count, start = struct.unpack_from("<II", data, 88)
    methods = []
    for i in range(count):
        owner, proto, name = struct.unpack_from("<HHI", data, start + 8 * i)
        if "alight" in types[owner].lower():
            methods.append({"class": types[owner], "name": strings[name], "prototype_index": proto})
    return {"classes": classes, "reference_methods": methods,
            "string_count": len(strings), "strings": strings}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("apk", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = args.output.resolve()
    if "build" not in root.parts:
        parser.error("Reference output must be inside an ignored build directory")
    root.mkdir(parents=True, exist_ok=True)
    archive_root = root / "unpacked"
    entries, effects, dexes = [], [], []
    layouts = []
    with zipfile.ZipFile(args.apk) as archive:
        infos = archive.infolist()
        if sum(info.file_size for info in infos) > 2_000_000_000:
            raise ValueError("Archive exceeds the static inspection budget")
        seen = set()
        for info in infos:
            relative = PurePosixPath(info.filename)
            if relative.is_absolute() or ".." in relative.parts or ":" in info.filename or "\\" in info.filename:
                raise ValueError(f"Unsafe archive path: {info.filename!r}")
            path = (archive_root / info.filename).resolve()
            if not path.is_relative_to(archive_root) or str(path).lower() in seen:
                raise ValueError(f"Duplicate or escaping archive path: {info.filename!r}")
            seen.add(str(path).lower())
            if info.is_dir():
                path.mkdir(parents=True, exist_ok=True)
                continue
            data = archive.read(info)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            entries.append({"path": info.filename, "bytes": len(data),
                            "sha256": hashlib.sha256(data).hexdigest()})
            if relative.parts[:2] == ("assets", "effects") and len(relative.parts) == 3 and relative.suffix == ".xml":
                node = ET.fromstring(data)
                if node.tag == "effect":
                    controls = [{"kind": p.tag, **p.attrib,
                                 "choices": [c.attrib for c in p if c.tag == "choice"]}
                                for p in node.findall("./params/*") if p.get("id")]
                    effects.append({"path": info.filename, **node.attrib, "controls": controls})
            if len(relative.parts) == 1 and relative.suffix == ".dex":
                index = dex_index(data)
                (root / (relative.name + ".index.json")).write_text(json.dumps(index, ensure_ascii=False, indent=2), encoding="utf-8")
                dexes.append({"path": relative.name, "classes": len(index["classes"]),
                              "strings": index["string_count"], "reference_methods": len(index["reference_methods"])})
            if info.filename.startswith("res/layout"):
                layouts.append(info.filename)
    result = {"apk_name": args.apk.name, "apk_sha256": hashlib.file_digest(args.apk.open("rb"), "sha256").hexdigest(),
              "entry_count": len(entries), "uncompressed_bytes": sum(x["bytes"] for x in entries),
              "effects": len(effects), "controls": sum(len(e["controls"]) for e in effects),
              "layouts": len(layouts), "dex": dexes,
              "top_level": dict(collections.Counter(e["path"].split("/")[0] for e in entries)),
              "limitations": ["Static evidence only; no UI interaction or pixel equivalence verified.",
                              "Modified APK provenance and completeness are not established.",
                              "DEX symbol/string index is not decompiled implementation."]}
    for filename, content in (("summary.json", result), ("files.json", entries),
                              ("effects.json", effects), ("layouts.json", layouts)):
        (root / filename).write_text(json.dumps(content, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
