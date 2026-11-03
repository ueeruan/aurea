"""Datas dos arquivos entre rodadas da CI (build incremental do iOS).

O checkout do GitHub grava todos os arquivos com a data de AGORA: o Xcode e o
Ninja acham que tudo mudou e recompilam do zero, mesmo com o DerivedData e a
pasta do CMake restaurados do cache. Este script guarda, no fim da rodada, o
hash (blob do git) e a data de cada arquivo versionado; na rodada seguinte,
devolve a data antiga a quem tem o MESMO hash. Só o que mudou de verdade fica
"novo" e é recompilado.

Uso:
    python3 tools/ci_mtimes.py restore build/ci-mtimes.json
    python3 tools/ci_mtimes.py save    build/ci-mtimes.json
"""
import json
import os
import subprocess
import sys


def tracked():
    """{caminho: blob} de todos os arquivos versionados (sem ler os arquivos)."""
    out = subprocess.run(["git", "ls-files", "-s", "-z"], check=True, capture_output=True).stdout
    files = {}
    for entry in out.split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        files[path.decode("utf-8", "surrogateescape")] = meta.split()[1].decode()
    return files


def save(manifest):
    data = {}
    for path, blob in tracked().items():
        try:
            data[path] = [blob, os.stat(path).st_mtime_ns]
        except OSError:
            pass
    os.makedirs(os.path.dirname(manifest) or ".", exist_ok=True)
    with open(manifest, "w", encoding="utf-8") as f:
        json.dump(data, f)
    print(f"ci_mtimes: {len(data)} arquivos guardados")


def restore(manifest):
    try:
        with open(manifest, encoding="utf-8") as f:
            old = json.load(f)
    except (OSError, ValueError):
        print("ci_mtimes: sem manifesto anterior (build completo)")
        return
    same = changed = 0
    for path, blob in tracked().items():
        prev = old.get(path)
        if prev and prev[0] == blob:
            try:
                os.utime(path, ns=(prev[1], prev[1]))
                same += 1
            except OSError:
                pass
        else:
            changed += 1
    print(f"ci_mtimes: {same} arquivos iguais com a data antiga, {changed} novos ou mudados")


if __name__ == "__main__":
    if len(sys.argv) != 3 or sys.argv[1] not in ("save", "restore"):
        sys.exit(__doc__)
    (save if sys.argv[1] == "save" else restore)(sys.argv[2])
