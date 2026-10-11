"""Decode every exported picture and validate its absolute A/V clock.

Uses an independently installed FFmpeg executable, not the app's encoder or
success flag. Returns failure on decoder errors, wrong picture count, wrong
dimensions, missing audio, or timing discontinuities. Inputs are owned
exports; raw inspection data is retained exclusively under build/reference/.
"""
from __future__ import annotations

import argparse
import array
from fractions import Fraction
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys


def command_json(command: list[str]) -> dict:
    run = subprocess.run(command, capture_output=True, text=True, check=True)
    return json.loads(run.stdout)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("file", type=Path)
    parser.add_argument("--frames", type=int, required=True)
    parser.add_argument("--fps", type=Fraction, required=True)
    parser.add_argument("--width", type=int, required=True)
    parser.add_argument("--height", type=int, required=True)
    parser.add_argument("--audio", action="store_true")
    parser.add_argument("--expected-tones", help="Known stereo frequencies after source time mapping, left,right in Hz")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    parser.add_argument("--output", type=Path, default=Path("build/reference/astra-validation"))
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    evidence_root = (root / "build/reference").resolve()
    destination = args.output.resolve()
    if destination != evidence_root and evidence_root not in destination.parents:
        parser.error("Raw validation files must remain under build/reference/")
    if args.frames <= 0 or args.fps <= 0 or args.width <= 0 or args.height <= 0:
        parser.error("Expected frame count, fps and dimensions must be positive")
    expected_tones = None
    if args.expected_tones:
        try:
            expected_tones = [float(value) for value in args.expected_tones.split(",")]
            if not args.audio or len(expected_tones) != 2 or any(not 0 < value < 24000 for value in expected_tones):
                raise ValueError()
        except ValueError:
            parser.error("--expected-tones requires --audio and two positive stereo frequencies below Nyquist")
    destination.mkdir(parents=True, exist_ok=True)
    movie = args.file.resolve()
    stem = movie.stem
    checks: list[str] = []
    result = {"file": str(movie), "passed": False, "expected": {
        "frames": args.frames, "fps": str(args.fps), "width": args.width,
        "height": args.height, "audio": args.audio}}
    try:
        metadata = command_json([args.ffprobe, "-v", "error", "-show_streams",
                                 "-show_format", "-of", "json", str(movie)])
        (destination / f"{stem}-streams.json").write_text(
            json.dumps(metadata, indent=2), encoding="utf-8")
        video = [s for s in metadata["streams"] if s["codec_type"] == "video"]
        audio = [s for s in metadata["streams"] if s["codec_type"] == "audio"]
        if len(video) != 1 or len(audio) != int(args.audio):
            raise ValueError(f"Expected one video and {int(args.audio)} audio streams; found {len(video)}/{len(audio)}")
        if (video[0]["width"], video[0]["height"]) != (args.width, args.height):
            raise ValueError("Decoded picture dimensions do not match the requested dimensions")
        if video[0].get("color_space") != "bt709" or video[0].get("color_range") != "tv":
            raise ValueError("Expected limited-range BT.709 color metadata")
        checks.append("streams/dimensions/color")

        frame_log = destination / f"{stem}-frames.txt"
        errors = destination / f"{stem}-decode-errors.txt"
        video_frames = 0
        audio_samples = 0
        first_audio = None
        last_audio_end = None
        clock_tolerance = max(Fraction(2, 1_000_000), Fraction(video[0].get("time_base", "1/1000000")))
        with errors.open("w", encoding="utf-8") as stderr, frame_log.open("w", encoding="utf-8") as evidence:
            decode = subprocess.Popen([
                args.ffprobe, "-v", "error", "-err_detect", "explode", "-show_frames",
                "-show_entries", "frame=media_type,best_effort_timestamp_time,nb_samples,width,height",
                "-of", "compact=p=0:nk=0", str(movie)], stdout=subprocess.PIPE,
                stderr=stderr, text=True)
            assert decode.stdout is not None
            try:
                for line in decode.stdout:
                    evidence.write(line)
                    fields = dict(item.split("=", 1) for item in line.strip().split("|") if "=" in item)
                    kind = fields.get("media_type")
                    if kind not in ("video", "audio"):
                        continue
                    if "best_effort_timestamp_time" not in fields:
                        raise ValueError("Decoded picture/sample block has no presentation time")
                    stamp = Fraction(fields["best_effort_timestamp_time"])
                    if kind == "video":
                        expected = Fraction(video_frames, 1) / args.fps
                        if abs(stamp - expected) > clock_tolerance:
                            raise ValueError(f"Decoded picture {video_frames}: PTS {stamp}, expected {expected}")
                        if (int(fields["width"]), int(fields["height"])) != (args.width, args.height):
                            raise ValueError(f"Picture dimensions changed at frame {video_frames}")
                        video_frames += 1
                        if video_frames > args.frames:
                            raise ValueError("Decoder returned more pictures than the project clock requires")
                    else:
                        rate = int(audio[0]["sample_rate"])
                        count = int(fields["nb_samples"])
                        if first_audio is None:
                            first_audio = stamp
                        if last_audio_end is not None and abs(stamp - last_audio_end) > Fraction(2, rate):
                            raise ValueError(f"Decoded audio discontinuity: {stamp} after {last_audio_end}")
                        last_audio_end = stamp + Fraction(count, rate)
                        audio_samples += count
                if decode.wait() != 0:
                    raise ValueError("Independent decoder rejected the exported file")
            finally:
                if decode.poll() is None:
                    decode.kill()
                    decode.wait()
        if errors.stat().st_size:
            raise ValueError("Independent decoder produced errors; inspect the retained stderr")
        if video_frames != args.frames:
            raise ValueError(f"Decoded {video_frames} pictures; expected {args.frames}")
        result["decodedFrames"] = video_frames
        checks.append("all decoded picture PTS/count")

        if args.audio:
            rate = int(audio[0]["sample_rate"])
            duration = Fraction(args.frames, 1) / args.fps
            padding = Fraction(2048, rate)
            if first_audio is None or last_audio_end is None or abs(first_audio) > padding or abs(last_audio_end - duration) > padding:
                raise ValueError("Decoded audio boundaries are not synchronized to the output video clock")
            levels = subprocess.run([
                args.ffmpeg, "-hide_banner", "-v", "info", "-xerror", "-i", str(movie),
                "-map", "0:a:0", "-af", "astats=metadata=1:reset=0", "-f", "null", "-"],
                capture_output=True, text=True, check=True)
            (destination / f"{stem}-audio-levels.txt").write_text(levels.stderr, encoding="utf-8")
            # Silence can be intentional. This script verifies complete audio
            # decode/timing; source-specific audible-tone assertions live in
            # the native regression tests, where the source is known.
            result["decodedAudioSamples"] = audio_samples
            checks.append("all decoded audio continuity/boundaries")
            if expected_tones:
                raw = subprocess.run([args.ffmpeg, "-hide_banner", "-v", "error", "-xerror",
                    "-i", str(movie), "-map", "0:a:0", "-ac", "2", "-ar", "48000",
                    "-f", "f32le", "-"], capture_output=True, check=True).stdout
                (destination / f"{stem}-stereo.f32le").write_bytes(raw)
                values = array.array("f"); values.frombytes(raw)
                if sys.byteorder != "little": values.byteswap()
                begin = int(float(duration) * .25 * 48000)
                end = int(float(duration) * .75 * 48000)
                if end <= begin or len(values) < end * 2:
                    raise ValueError("Decoded PCM does not cover the known source-clock interval")
                measurements = []
                for channel, frequency in enumerate(expected_tones):
                    sine = cosine = power = 0.0
                    for sample in range(begin, end):
                        value = values[sample * 2 + channel]
                        phase = 2 * math.pi * frequency * sample / 48000
                        sine += value * math.sin(phase); cosine += value * math.cos(phase)
                        power += value * value
                    correlation = math.sqrt(2 * (sine * sine + cosine * cosine) / ((end - begin) * power)) if power else 0
                    phase_error = math.atan2(cosine, sine)
                    measurements.append({"channel": channel, "expectedHz": frequency,
                        "correlation": correlation, "phaseRadians": phase_error})
                    if correlation < .97 or abs(phase_error) > .35:
                        raise ValueError(f"Stereo channel {channel} does not follow the source clock: {measurements[-1]}")
                result["sourceClockToneMeasurements"] = measurements
                checks.append("known stereo frequency/phase against absolute video clock")

        hashes = destination / f"{stem}-framemd5.txt"
        with hashes.open("w", encoding="utf-8") as stdout:
            subprocess.run([
                args.ffmpeg, "-hide_banner", "-v", "error", "-xerror", "-err_detect", "explode",
                "-i", str(movie), "-map", "0:v:0", "-fps_mode", "passthrough",
                "-f", "framemd5", "-"], stdout=stdout, stderr=subprocess.PIPE,
                text=True, check=True)
        hash_count = sum(1 for line in hashes.read_text(encoding="utf-8").splitlines() if line and not line.startswith("#"))
        if hash_count != args.frames:
            raise ValueError(f"Independent pixel hashing decoded {hash_count} pictures; expected {args.frames}")
        digest = hashlib.sha256()
        with movie.open("rb") as source:
            for block in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(block)
        result.update(passed=True, sha256=digest.hexdigest(), checks=checks, pixelHashes=hash_count)
        print(f"PASS {movie.name}: {video_frames} decoded pictures with exact PTS; audio samples={audio_samples}")
    except (ValueError, OSError, subprocess.SubprocessError, KeyError) as error:
        result["error"] = str(error)
        print(f"FAIL {movie.name}: {error}", file=sys.stderr)
    (destination / f"{stem}-result.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
