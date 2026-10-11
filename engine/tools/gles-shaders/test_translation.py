"""Real tool regression: full GLES shape source uses the shared SPIR-V key.

Run after building aurea-gles-compiler, with --tool, --glslc and --output-dir.
This checks the build-time translator; it does not claim mobile GPU validation.
"""
import argparse
import hashlib
import json
import mmap
from pathlib import Path
import re
import shutil
import subprocess


def fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f"{value:x}"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--tool", type=Path, required=True)
    parser.add_argument("--glslc", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--production-build", type=Path)
    parser.add_argument("--production-elf", type=Path)
    args = parser.parse_args()
    if bool(args.production_build) != bool(args.production_elf):
        parser.error("--production-build and --production-elf must be specified together")
    shader_root = Path(__file__).resolve().parents[2] / "shaders"
    args.output_dir.mkdir(parents=True, exist_ok=True)
    records = []

    def run(name, command, expect_failure=False):
        result = subprocess.run([str(arg) for arg in command], capture_output=True, timeout=30)
        (args.output_dir / (name + ".log")).write_bytes(result.stdout + result.stderr)
        records.append({"step": name, "exitCode": result.returncode})
        if (result.returncode != 0) != expect_failure:
            raise RuntimeError(f"{name}: unexpected exit {result.returncode}")

    run("capability", [args.tool, "--supports-shared-spv-key"])
    shared = args.output_dir / "shape-shared.frag.spv"
    alternate = args.output_dir / "shape-gles.frag.spv"
    for name, optimize, target in [("shared", "-O", shared), ("alternate", "-O0", alternate)]:
        run(name, [args.glslc, "--target-env=vulkan1.0", optimize, "-Werror", "-I", shader_root,
                   "-o", target, shader_root / "shape/shape.frag"])
    default = args.output_dir / "shape-default.gles"
    keyed = args.output_dir / "shape-keyed.gles"
    run("default-api", [args.tool, alternate, default])
    run("shared-key-api", [args.tool, alternate, keyed, shared])
    expected_shared = fingerprint(shared.read_bytes())
    expected_alternate = fingerprint(alternate.read_bytes())
    default_lines = default.read_text().splitlines(keepends=True)
    keyed_lines = keyed.read_text().splitlines(keepends=True)
    assert expected_shared != expected_alternate, "fixture did not exercise alternate translation"
    assert default_lines[0].strip() == "// AUREA_SPIRV_FNV64 " + expected_alternate
    assert keyed_lines[0].strip() == "// AUREA_SPIRV_FNV64 " + expected_shared
    assert default_lines[1:] == keyed_lines[1:], "lookup key changed the shader implementation"
    source = "".join(keyed_lines[1:])
    assert source.startswith("#version 310 es")
    for function in ("sd_heart", "sd_polygon", "sd_bolt", "wave_distance", "sd_blob"):
        assert re.search(r"\b" + function + r"\s*\(", source), "missing full shape function: " + function
    for kind in (0, 1, 3, 4, 5, 6, 7, 8, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24):
        assert re.search(r"\btype\s*==\s*" + str(kind) + r"\b", source), "missing shape branch: " + str(kind)
    invalid = args.output_dir / "invalid.spv"
    invalid.write_bytes(b"not valid SPIR-V" + b"\0" * 9)
    rejected = args.output_dir / "invalid-key.gles"
    run("invalid-key", [args.tool, alternate, rejected, invalid], expect_failure=True)
    assert not rejected.exists(), "invalid fingerprint module produced an ESSL source"
    report = {"result": "PASS", "scope": "executed host translator; mobile pixels checked separately",
              "sharedFingerprint": expected_shared, "alternateFingerprint": expected_alternate,
              "esslSHA256": hashlib.sha256(keyed.read_bytes()).hexdigest(), "steps": records,
              "shaderBranches": 23, "fullFunctionsPreserved": True,
              "toolSHA256": hashlib.sha256(args.tool.read_bytes()).hexdigest(),
              "toolSourceSHA256": hashlib.sha256(Path(__file__).with_name("main.cpp").read_bytes()).hexdigest(),
              "glslcSHA256": hashlib.sha256(args.glslc.read_bytes()).hexdigest(),
              "shapeSourceSHA256": hashlib.sha256((shader_root / "shape/shape.frag").read_bytes()).hexdigest()}
    if args.production_build:
        generated = args.production_build / "generated/shaders/shape"
        matches = [(generated / "shape.frag.spv", shared, "production-shape-shared.spv"),
                   (generated / "shape.frag.spv.gles-source.spv", alternate, "production-shape-gles.spv"),
                   (generated / "shape.frag.spv.gles", keyed, "production-shape.frag.gles")]
        production = {}
        for actual, tested, name in matches:
            assert actual.read_bytes() == tested.read_bytes(), "production translation differs: " + str(actual)
            shutil.copy2(actual, args.output_dir / name)
            production[name] = {"bytes": actual.stat().st_size,
                                "sha256": hashlib.sha256(actual.read_bytes()).hexdigest()}
        blob = args.production_build / "gpu/gles/GlesShaderBlobs.cpp"
        entry = '{0x' + expected_shared + 'ull, R"AUREAGLES(' + keyed.read_text() + ')AUREAGLES"},'
        assert blob.read_text().count(entry) == 1, "built shader blob does not contain the complete keyed shape"
        payload = keyed.read_bytes() + b"\0"
        with args.production_elf.open("rb") as stream:
            with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as binary:
                offset = binary.find(payload)
                assert offset >= 0, "linked GLES ELF does not contain the executed complete shape source"
                assert binary.find(payload, offset + len(payload)) == -1, "ambiguous shape source in linked ELF"
                elf_sha256 = hashlib.sha256(binary).hexdigest()
        report["production"] = {"files": production, "exactBuildFilesChecked": True,
                                "embeddedEntryChecked": True, "linkedElfSourceChecked": True,
                                "linkedElfSHA256": elf_sha256, "linkedEsslOffset": offset,
                                "linkedEsslBytes": len(payload),
                                "shaderBlobsSHA256": hashlib.sha256(blob.read_bytes()).hexdigest()}
    (args.output_dir / "translation-regression.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report))


if __name__ == "__main__":
    main()
