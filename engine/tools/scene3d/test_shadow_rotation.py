"""Validate both real shader paths and bound float32 Vogel coordinate drift.

This is compiler/math evidence. Pixel parity and timing require the native
baseline/target A/B probes with unchanged authored shadow settings.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess


def f32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


def coordinate_error(source):
    values = re.search(r"const vec2 gold = vec2\(([-\d.]+),\s*([-\d.]+)\)", source)
    assert values, "rotation constant is missing"
    cosine, sine = (f32(float(value)) for value in values.groups())
    golden = f32(2.39996323)
    assert cosine == f32(math.cos(golden)) and sine == f32(math.sin(golden))
    maximum, worst = 0.0, None
    for sample in range(1025):
        phi = f32(sample * math.tau / 1024)
        origin = f32(math.cos(phi)), f32(math.sin(phi))
        for n in range(1, 33):
            direction = origin
            for i in range(n):
                radius = f32(math.sqrt(f32((i + .5) / n)))
                theta = f32(f32(i * golden) + phi)
                legacy = f32(radius * f32(math.cos(theta))), f32(radius * f32(math.sin(theta)))
                rotated = f32(radius * direction[0]), f32(radius * direction[1])
                delta = max(abs(a - b) for a, b in zip(legacy, rotated))
                if delta > maximum:
                    maximum, worst = delta, {"phiSample": sample, "samples": n, "index": i}
                x, y = direction
                direction = f32(f32(cosine * x) - f32(sine * y)), f32(f32(sine * x) + f32(cosine * y))
    assert maximum < 5.6e-6, f"coordinate drift exceeds the reviewed float32 bound: {maximum}"
    return {"phiSamples": 1025, "sampleCounts": [1, 32], "maximumUnitDiskComponentError": maximum,
            "worst": worst, "maximumTexelOffset4096Radius012": maximum * 4096 * .12,
            "maximumTexelOffsetFixedPcfRadius15Texels": maximum * 1.5}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslc", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--essl-tool", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2] / "shaders"
    args.output_dir.mkdir(parents=True, exist_ok=True)
    shadow = root / "scene3d/common/shadow.glsl"
    math_result = coordinate_error(shadow.read_text(encoding="utf-8"))
    isolated = args.output_dir / "shadow-isolated.frag"
    isolated.write_text('''#version 450
#include "scene3d/common/shadow.glsl"
layout(set = 0, binding = 0) uniform sampler2DShadow comparisonMap;
layout(set = 0, binding = 1) uniform sampler2D depthMap;
layout(push_constant) uniform Probe { vec4 params; vec4 params2; } u;
layout(location = 0) out vec4 color;
void main() {
    vec2 pixel = gl_FragCoord.xy;
    vec3 world = vec3(pixel / vec2(320.0, 180.0), 0.6);
    float shadow = aurea_shadow(comparisonMap, depthMap, mat4(1.0), u.params, u.params2,
                               world, normalize(vec3(0.2, 0.1, 1.0)), vec3(0.0, 0.0, 1.0), pixel);
    color = vec4(shadow, shadow, shadow, 1.0);
}
''', encoding="utf-8")
    records = []
    for name, path in [("isolated", isolated), ("pbr", root / "scene3d/pbr/pbr.frag"),
                       ("ground", root / "scene3d/ground.frag")]:
        hashes = []
        for variant in ("reference", "rotation"):
            target = args.output_dir / f"{name}-{variant}.frag.spv"
            command = [args.glslc, "--target-env=vulkan1.0", "-O", "-Werror", "-I", root]
            if variant == "reference":
                command.append("-DAUREA_SHADOW_REFERENCE_VOGEL=1")
            command.extend(["-o", target, path])
            result = subprocess.run([str(value) for value in command], capture_output=True, timeout=30)
            (args.output_dir / f"{name}-{variant}.log").write_bytes(result.stdout + result.stderr)
            assert result.returncode == 0, f"{name}/{variant}: shader compiler failed"
            digest = hashlib.sha256(target.read_bytes()).hexdigest()
            hashes.append(digest)
            records.append({"shader": name, "variant": variant, "exitCode": result.returncode,
                            "spirvSha256": digest, "spirvBytes": target.stat().st_size})
            if args.essl_tool:
                translated = target.with_suffix(".spv.gles")
                result = subprocess.run([str(args.essl_tool), str(target), str(translated)],
                                        capture_output=True, timeout=30)
                (args.output_dir / f"{name}-{variant}-essl.log").write_bytes(result.stdout + result.stderr)
                assert result.returncode == 0, f"{name}/{variant}: ESSL translation failed"
                assert translated.read_text().splitlines()[1].strip() == "#version 310 es"
        assert hashes[0] != hashes[1], f"{name}: reference did not exercise a different shader path"
    report = {"shaderSourceSha256": hashlib.sha256(shadow.read_bytes()).hexdigest(),
              "compilePassed": True, "records": records, "float32MathProxy": math_result,
              "gpuPixelParityProven": False, "gpuPerformanceProven": False,
              "warning": "Depth comparisons are discontinuous; this coordinate bound does not prove pixel parity."}
    (args.output_dir / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
