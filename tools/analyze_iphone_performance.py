"""Summarize an Aurea iPhone .jsonl report; no network or media access.

Usage: python tools/analyze_iphone_performance.py report.jsonl
Values are sampled observations, not a proof of the cause of a stall.
"""
import argparse
import json
import math
from pathlib import Path


def number(value):
    return value if isinstance(value, (float, int)) and not isinstance(value, bool) and math.isfinite(value) else None


def summarize(rows):
    starts = [r for r in rows if r.get("event") == "session_start"]
    ends = [r for r in rows if r.get("event") == "session_end"]
    phases = {}
    for row in rows:
        if row.get("event") != "sample" or not isinstance(row.get("perf"), dict):
            continue
        p = row["perf"]
        phase = phases.setdefault(str(row.get("phase", "unknown")), {
            "samples": 0, "playingSamples": 0, "sampledMaxCPUms": 0,
            "sampledMaxGPUms": None, "sampledMaxDecodeMs": 0,
            "maxProcessFootprintBytes": None, "maxMainThreadGapMs": 0,
            "maxRollingPacingP95Ms": None, "fpsSumWhilePlaying": 0,
            "fpsSamplesWhilePlaying": 0, "firstDroppedCounter": None, "lastDroppedCounter": None,
        })
        phase["samples"] += 1
        phase["playingSamples"] += int(row.get("playing") is True)
        for source, dest in [("cpuFrameMs", "sampledMaxCPUms"), ("decodeMs", "sampledMaxDecodeMs")]:
            n = number(p.get(source))
            if n is not None:
                phase[dest] = max(phase[dest], n)
        for source, dest, valid in [
            ("gpuFrameMs", "sampledMaxGPUms", p.get("gpuTimers", 0)),
            ("processFootprintBytes", "maxProcessFootprintBytes", p.get("processFootprintAvailable", False)),
            ("pacingP95Ms", "maxRollingPacingP95Ms", p.get("pacingSamples", 0)),
        ]:
            n = number(p.get(source))
            if valid and n is not None:
                phase[dest] = max(phase[dest] or 0, n)
        n = number(row.get("uiMaxGapMs"))
        if n is not None:
            phase["maxMainThreadGapMs"] = max(phase["maxMainThreadGapMs"], n)
        n = number(p.get("previewFps"))
        if row.get("playing") is True and n is not None:
            phase["fpsSumWhilePlaying"] += n
            phase["fpsSamplesWhilePlaying"] += 1
        n = number(p.get("droppedFrames"))
        if n is not None:
            if phase["firstDroppedCounter"] is None:
                phase["firstDroppedCounter"] = n
            phase["lastDroppedCounter"] = n
    for phase in phases.values():
        count = phase.pop("fpsSamplesWhilePlaying")
        total = phase.pop("fpsSumWhilePlaying")
        phase["meanSampledPreviewFpsWhilePlaying"] = total / count if count else None
        first, last = phase.pop("firstDroppedCounter"), phase.pop("lastDroppedCounter")
        phase["observedDroppedCounterDelta"] = last - first if first is not None and last >= first else None
    return {"session": starts[0] if starts else None, "complete": bool(ends),
            "end": ends[-1] if ends else None, "phases": phases,
            "events": [r for r in rows if r.get("event") in {
                "ui_gap", "main_thread_unresponsive", "user_reported_stall", "memory_warning", "import_start", "import_end"}],
            "interpretation": "Sampled metrics and correlated events; unavailable GPU/OS memory values remain null. A partial report does not identify a crash cause."}


def read_report(path):
    if path.stat().st_size > 16 * 1024 * 1024:
        raise ValueError("Report exceeds 16 MiB")
    rows, invalid = [], []
    for line_number, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
        try:
            row = json.loads(line)
            if not isinstance(row, dict):
                raise ValueError("not an object")
            rows.append(row)
        except (ValueError, json.JSONDecodeError):
            invalid.append(line_number)
    result = summarize(rows)
    result["invalidLines"] = invalid
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    args = parser.parse_args()
    print(json.dumps(read_report(args.report), ensure_ascii=False, indent=2))
