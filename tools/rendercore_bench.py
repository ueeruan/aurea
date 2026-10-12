"""Benchmark do export no aparelho (Missão ASTRA / RenderCore).

Roda RenderCoreBenchmarkDeviceTest um cenário por vez (processo novo = pico de
memória do próprio cenário), junta as linhas AureaBench do teste com as etapas
que o motor registra ("export N/N ... ms/quadro", "export perfil") e grava um
JSON em build/reference/rendercore-bench/<rotulo>.json.

    python tools/rendercore_bench.py --label base --install
    python tools/rendercore_bench.py --label novo --scenarios staticTitle,videoClip
    python tools/rendercore_bench.py --compare base novo
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "build" / "reference" / "rendercore-bench"
PKG = "com.aurea.aurea.uitest"
RUNNER = f"{PKG}.test/androidx.test.runner.AndroidJUnitRunner"
TEST = "com.aurea.aurea.editor.RenderCoreBenchmarkDeviceTest"
SCENARIOS = ["staticTitle", "photoSlideshow", "videoClip", "videoEffects", "project13", "project8"]
EXT = f"/sdcard/Android/data/{PKG}/files"
FIXTURES = {
    "galeria-teste.mp4": "/sdcard/Movies/galeria-teste.mp4",
    "Project 13.aureaproj": "/sdcard/Download/Project 13.aureaproj",
    "Project8-Test-copy.aureaproj": "/sdcard/Download/Project8-Test-copy.aureaproj",
}


def adb(*args: str, check: bool = True, timeout: int | None = None) -> str:
    env = dict(os.environ, MSYS_NO_PATHCONV="1")
    proc = subprocess.run(["adb", *args], capture_output=True, text=True, encoding="utf-8",
                          errors="replace", env=env, timeout=timeout)
    if check and proc.returncode != 0:
        raise RuntimeError(f"adb {' '.join(args)} falhou: {proc.stderr.strip()}")
    return proc.stdout


def install(apk_dir: str | None) -> None:
    apk = ROOT / "build" / "android" / "app" / "outputs" / "apk"
    app = apk / "uiTest" / "app-uiTest.apk"
    test = apk / "androidTest" / "uiTest" / "app-uiTest-androidTest.apk"
    if apk_dir:
        app = Path(apk_dir) / "app-uiTest.apk"
        test = Path(apk_dir) / "app-uiTest-androidTest.apk"
    for path in (app, test):
        if not path.is_file():
            raise SystemExit(f"APK ausente: {path}")
        print("instalando", path.name)
        adb("install", "-r", "-t", str(path), timeout=600)


def prepare_fixtures() -> None:
    # A pasta externa do app existe depois da primeira execução; cria por garantia.
    adb("shell", f"mkdir -p {EXT}")
    for name, source in FIXTURES.items():
        target = f"{EXT}/{name}"
        have = adb("shell", f"ls -l '{target}' 2>/dev/null", check=False).strip()
        if have:
            continue
        if not adb("shell", f"ls '{source}' 2>/dev/null", check=False).strip():
            print("fixture ausente no aparelho:", source)
            continue
        print("copiando", source)
        adb("shell", f"cp '{source}' '{target}'", timeout=600)


def thermal() -> str:
    text = adb("shell", "dumpsys thermalservice", check=False)
    match = re.search(r"Thermal Status:\s*(\d+)", text)
    temps = re.findall(r"mValue=([\d.]+), mType=0, mName=(\S+?),", text)
    cpu = max((float(v) for v, _ in temps), default=-1.0)
    return f"status={match.group(1) if match else '?'} cpuMaxC={cpu:.1f}"


STAGE = re.compile(r"export (\d+)/(\d+) \((\d+) em voo\): decode ([\d.]+) render ([\d.]+) leitura ([\d.]+) encoder ([\d.]+) ms/quadro")
PROFILE = re.compile(r"export perfil: (.*)")
KV = re.compile(r"(\w+)=([^\s]+)")


def run_scenario(name: str, extra: list[str]) -> dict:
    adb("logcat", "-c", check=False)
    before = thermal()
    began = time.time()
    cmd = ["shell", "am", "instrument", "-w", "-e", "aureaBench", "true", *extra,
           "-e", "class", f"{TEST}#{name}", RUNNER]
    output = adb(*cmd, check=False, timeout=7200)
    elapsed = time.time() - began
    log = adb("logcat", "-d", "-v", "time", check=False)
    result: dict = {"scenario": name, "instrumentSeconds": round(elapsed, 1), "thermalBefore": before,
                    "thermalAfter": thermal(), "passed": "OK (1 test)" in output}
    stages, extras = [], []
    for line in log.splitlines():
        if "AureaBench" in line and ("BEGIN" in line or "END" in line):
            kind = "begin" if "BEGIN" in line else "end"
            result[kind] = dict(KV.findall(line.split("AureaBench", 1)[1]))
        elif (m := STAGE.search(line)):
            stages.append({"done": int(m.group(1)), "total": int(m.group(2)), "depth": int(m.group(3)),
                           "decode": float(m.group(4)), "render": float(m.group(5)),
                           "readback": float(m.group(6)), "encoder": float(m.group(7))})
        elif (m := PROFILE.search(line)):
            result["profile"] = m.group(1).strip()
        elif "AureaEngine" in line and ("export:" in line or "export falhou" in line or "rendercore" in line):
            extras.append(line.split("AureaEngine", 1)[1].strip()[:240])
    result["stages"] = stages
    result["engineNotes"] = extras[-40:]
    if not result["passed"]:
        result["instrumentTail"] = output[-1500:]
    return result


def summarize(entry: dict) -> str:
    end = entry.get("end", {})
    stages = entry.get("stages", [])
    total = max((s["done"] for s in stages), default=0)
    def weighted(key: str) -> float:
        prev, acc = 0, 0.0
        for s in stages:
            acc += s[key] * (s["done"] - prev); prev = s["done"]
        return acc / total if total else 0.0
    return (f"{entry['scenario']:<15} ok={entry['passed']!s:<5} wall={end.get('wallMs', '?'):>7} ms "
            f"fps={end.get('fps', '?'):>6} 1o={end.get('firstFrameMs', '?'):>6} ms final={end.get('finalizeMs', '?'):>6} ms "
            f"hwm={end.get('hwmKb', '?')} kB "
            f"| render {weighted('render'):6.1f} leitura {weighted('readback'):5.1f} encoder {weighted('encoder'):6.1f} ms/q")


def compare(a: str, b: str) -> None:
    left = {e["scenario"]: e for e in json.loads((OUT / f"{a}.json").read_text("utf-8"))["results"]}
    right = {e["scenario"]: e for e in json.loads((OUT / f"{b}.json").read_text("utf-8"))["results"]}
    print(f"{'cenario':<15} {a:>12} {b:>12}  ganho   pico RAM {a} -> {b}")
    for name in left:
        if name not in right:
            continue
        wa, wb = left[name].get("end", {}).get("wallMs"), right[name].get("end", {}).get("wallMs")
        ha, hb = left[name].get("end", {}).get("hwmKb"), right[name].get("end", {}).get("hwmKb")
        if not wa or not wb:
            print(f"{name:<15} sem medida completa"); continue
        gain = float(wa) / float(wb)
        print(f"{name:<15} {wa:>9} ms {wb:>9} ms  {gain:5.2f}x  {ha} -> {hb} kB")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--label", default="run")
    parser.add_argument("--scenarios", default=",".join(SCENARIOS))
    parser.add_argument("--install", action="store_true")
    parser.add_argument("--apk-dir", help="pasta com app-uiTest.apk e app-uiTest-androidTest.apk")
    parser.add_argument("--frames", type=int, default=0, help="limite de quadros (projetos reais)")
    parser.add_argument("--cooldown", type=int, default=20, help="segundos entre cenários")
    parser.add_argument("--compare", nargs=2)
    args = parser.parse_args()
    if args.compare:
        compare(*args.compare)
        return 0
    OUT.mkdir(parents=True, exist_ok=True)
    if args.install:
        install(args.apk_dir)
    prepare_fixtures()
    device = adb("shell", "getprop ro.product.model").strip()
    extra = ["-e", "aureaBenchFrames", str(args.frames)] if args.frames else []
    results = []
    for i, name in enumerate([s for s in args.scenarios.split(",") if s]):
        if i:
            time.sleep(args.cooldown)
        entry = run_scenario(name, extra)
        results.append(entry)
        print(summarize(entry), flush=True)
        (OUT / f"{args.label}.json").write_text(json.dumps({"label": args.label, "device": device,
            "frames": args.frames, "results": results}, indent=2, ensure_ascii=False), "utf-8")
    return 0 if all(r["passed"] for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
