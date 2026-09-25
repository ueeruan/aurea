"""Compare a generated timing fixture with an editor export using independent decoding.

This is a regression tool for distinct test-pattern frames, not a perceptual
quality score or a general comparison for footage containing identical frames.
"""
import argparse
import bisect
import json
import math
import subprocess
from pathlib import Path


def decode_samples(ffmpeg, path):
    result = subprocess.run([
        ffmpeg, "-v", "error", "-i", str(path), "-vf", "scale=160:90",
        "-fps_mode", "passthrough", "-pix_fmt", "rgb24", "-f", "rawvideo", "pipe:1",
    ], capture_output=True, check=True)
    frame_bytes = 160 * 90 * 3
    if not result.stdout or len(result.stdout) % frame_bytes:
        raise ValueError("incomplete decoded RGB frames")
    positions = [(y * 160 + x) * 3 + c
                 for y in range(4, 90, 8) for x in range(4, 160, 8) for c in range(3)]
    return [[result.stdout[start + i] for i in positions]
            for start in range(0, len(result.stdout), frame_bytes)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ffmpeg", required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--timestamps", type=Path, required=True)
    parser.add_argument("--export", dest="exported", type=Path, required=True)
    parser.add_argument("--composition-fps", type=float, required=True)
    parser.add_argument("--export-fps", type=float, required=True)
    parser.add_argument("--expected-frames", type=int, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    if not all(math.isfinite(fps) and fps > 0 for fps in (args.composition_fps, args.export_fps)):
        parser.error("frame rates must be finite and positive")
    source = decode_samples(args.ffmpeg, args.source)
    exported = decode_samples(args.ffmpeg, args.exported)
    pts = [int(value) for value in args.timestamps.read_text().split()]
    if len(pts) != len(source) or any(a >= b for a, b in zip(pts, pts[1:])):
        raise ValueError("source timestamps must match decoded frames and increase strictly")
    # The editor evaluates on the composition frame grid, including when export
    # FPS differs. Reference intervals are derived by FFmpeg, not by our decoder.
    expected = [bisect.bisect_right(pts, round(
        math.floor(i * args.composition_fps / args.export_fps + 1e-6)
        * 1e6 / args.composition_fps)) - 1 for i in range(len(exported))]
    nearest = []
    for frame in exported:
        errors = [sum(abs(a - b) for a, b in zip(frame, original)) for original in source]
        nearest.append(min(range(len(errors)), key=errors.__getitem__))
    mismatches = [i for i, (actual, wanted) in enumerate(zip(nearest, expected)) if actual != wanted]
    passed = len(exported) == args.expected_frames and not mismatches
    report = dict(passed=passed, frames=len(exported), sourceFrames=len(source),
                  compositionFps=args.composition_fps, exportFps=args.export_fps,
                  mismatches=mismatches, expectedIndices=expected, nearestIndices=nearest)
    args.report.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"{'PASS' if passed else 'FAIL'}: {len(exported)} frames, {len(mismatches)} presentation mismatches")
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
