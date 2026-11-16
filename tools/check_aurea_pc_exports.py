"""Aurea PC: confere se a aurea.dll exporta toda `external fun` do AureaEngine.kt.

Uso: python tools/check_aurea_pc_exports.py [caminho/da/aurea.dll]
(padrao: engine/build/pc-jni/bin/aurea.dll). Usa o dumpbin do MSVC.
Saida 0 = nenhuma faltando; 1 = faltam exportacoes (listadas).
"""
import glob
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
KOTLIN = ROOT / "android/app/src/main/java/com/aurea/aurea/engine/AureaEngine.kt"
PREFIX = "Java_com_aurea_aurea_engine_AureaEngine_"
DESKTOP = ["Java_com_aurea_aurea_desktop_DesktopFrames_nativeAcquireFrame",
           "Java_com_aurea_aurea_desktop_DesktopFrames_nativeGpuName", "JNI_OnLoad"]


def dumpbin() -> str:
    found = sorted(glob.glob(r"D:\Program Files (x86)\Microsoft Visual Studio\*\BuildTools\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe"))
    found += sorted(glob.glob(r"C:\Program Files*\Microsoft Visual Studio\*\*\VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe"))
    if not found:
        sys.exit("dumpbin.exe nao encontrado")
    return found[-1]


def main() -> int:
    dll = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "engine/build/pc-jni/bin/aurea.dll"
    # JNI escapa '_' do nome Kotlin como "_1"; nenhum nome aqui tem '_', mas fica correto.
    names = re.findall(r"external\s+fun\s+([A-Za-z0-9_]+)", KOTLIN.read_text(encoding="utf-8"))
    wanted = {PREFIX + n.replace("_", "_1") for n in names}
    out = subprocess.run([dumpbin(), "/exports", str(dll)], capture_output=True, text=True, errors="replace").stdout
    exported = set(re.findall(r"\s(Java_[A-Za-z0-9_]+|JNI_OnLoad)\s*$", out, re.MULTILINE))
    missing = sorted(wanted - exported)
    extra = sorted(e for e in exported if e.startswith(PREFIX) and e not in wanted)
    desktop_missing = [d for d in DESKTOP if d not in exported]
    print(f"Kotlin external fun: {len(names)} ({len(wanted)} nomes unicos)")
    print(f"Exportacoes AureaEngine na DLL: {sum(1 for e in exported if e.startswith(PREFIX))}")
    print(f"Faltando: {len(missing)}")
    for m in missing:
        print("  -", m)
    print(f"Exportadas sem external fun no Kotlin: {len(extra)}")
    for e in extra:
        print("  +", e)
    print("Desktop/JNI_OnLoad:", "ok" if not desktop_missing else f"faltando {desktop_missing}")
    return 1 if missing or desktop_missing else 0


if __name__ == "__main__":
    sys.exit(main())
