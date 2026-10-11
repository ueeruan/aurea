"""Create original lossless inputs for plugin/AUREA comparisons.

These are test patterns, not recreations or assets extracted from a plugin.
Only Python's standard library is required. Outputs belong in build/reference.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib


def png(width, height, pixels):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data
                + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))
    raw = b"".join(b"\0" + pixels[y * width * 4:(y + 1) * width * 4]
                   for y in range(height))
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def generate(width, height, out):
    colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 255),
              (255, 255, 0), (0, 255, 255), (255, 0, 255), (0, 0, 0)]
    def bars(x, y):
        return (*colors[min(7, x * 8 // width)], 255)
    def ramp(x, y):
        c = round(255 * x / max(1, width - 1))
        return c, c, c, 255
    def chroma(x, y):
        return (round(255 * x / max(1, width - 1)),
                round(255 * y / max(1, height - 1)),
                round(255 * (1 - x / max(1, width - 1))), 255)
    def checker(x, y):
        c = 255 if (x // 8 + y // 8) % 2 else 0
        return c, c, c, 255
    def lens(x, y):
        r = min(width, height) * 0.24
        d = math.hypot(x - (width - 1) / 2, y - (height - 1) / 2)
        c = round(255 * max(0, 1 - d / r))
        return c, c, c, 255
    def impulse(x, y):
        c = 255 if x == width // 2 and y == height // 2 else 0
        return c, c, c, 255
    def alpha(x, y):
        # Straight alpha: saturated RGB intentionally retained under alpha=0.
        return 255, 96, 16, round(255 * x / max(1, width - 1))
    def moving(frame):
        def pixel(x, y):
            cx = (frame * 3) % width
            c = 255 if abs(x - cx) <= 3 and abs(y - height // 2) <= 20 else 0
            return c, c, c, 255
        return pixel
    patterns = {"bars": bars, "gray-ramp": ramp, "rgb-ramp": chroma,
                "checker-8px": checker, "lens-cone": lens,
                "impulse": impulse, "straight-alpha-ramp": alpha}
    patterns.update({f"moving-{frame:03d}": moving(frame) for frame in range(60)})
    out.mkdir(parents=True, exist_ok=True)
    files = []
    for name, sample in patterns.items():
        pixels = bytearray()
        for y in range(height):
            for x in range(width):
                pixels.extend(sample(x, y))
        data = png(width, height, pixels)
        path = out / (name + ".png")
        path.write_bytes(data)
        files.append({"file": path.name, "sha256": hashlib.sha256(data).hexdigest()})
    manifest = {"schema": 1, "origin": "original mathematical test patterns",
                "width": width, "height": height, "format": "PNG RGBA8 straight alpha",
                "color_interpretation": "sRGB, set explicitly in both applications",
                "fps": 30, "moving_sequence_frames": 60,
                "claim": "Inputs only; no reference plugin renders or fidelity validation",
                "files": files}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Created {len(files)} original input PNGs and manifest in {out}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--width", type=int, default=320)
    parser.add_argument("--height", type=int, default=180)
    parser.add_argument("--out", type=Path,
                        default=Path(__file__).resolve().parent.parent
                        / "build/reference/requested-effects/inputs")
    args = parser.parse_args()
    if not (2 <= args.width <= 4096 and 2 <= args.height <= 4096):
        parser.error("dimensions must be between 2 and 4096")
    generate(args.width, args.height, args.out)
