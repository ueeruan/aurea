"""Converte o MiDaS v2.1 small (ONNX oficial) no grafo ncnn embutido do Aurea.

Não existe release ncnn oficial de profundidade: os bytes em
engine/assets/ai/midas-v21-small-256.{param,bin} saem DESTA receita, que é
determinística (os hashes esperados estão no README de engine/assets/ai).

    python tools/ai_depth_convert.py <model-small.onnx> <pasta de saída> \
        --pnnx <pnnx.exe 20250430> --ncnnoptimize <ncnnoptimize do ncnn fixado>

Passos:
 1. Restaura a semântica de Resize do TREINO. O ONNX oficial é opset 10, cujo
    Resize "linear" não sabe dizer align_corners: o PyTorch exportou as quatro
    fusões (align_corners=True) e a cabeça (align_corners=False) como o modo
    assimétrico do opset 10, e o mapa sai deslocado alguns pixels para cima e
    para a esquerda. Aqui o grafo sobe para o opset 11 (onnx.version_converter)
    e cada Resize ganha o coordinate_transformation_mode do treino: as quatro
    primeiras "align_corners", a última "half_pixel". Nenhum peso muda.
 2. pnnx 20250430 (o mais próximo do ncnn fixado, 20250503) converte para ncnn
    em fp32.
 3. Corrige o pnnx: ao fundir o Pad [topo 1, esquerda 1, baixo 2, direita 2] nas
    duas depthwise 5×5 de passo 2 ele perde o recuo do topo (14=0). Aqui volta
    a 14=1 — sem isto a altura cai de 8 para 7 no fim do encoder.
 4. ncnnoptimize do ncnn fixado com flag 65536: pesos em fp16 (~33 MB).
"""
import argparse
import hashlib
import pathlib
import re
import shutil
import subprocess
import sys

import onnx
from onnx import helper, version_converter

PNNX_FUSED_PAD_FIX = ("convdwclip_4", "convdwclip_17")


def sha256(path: pathlib.Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def restore_resize(src: pathlib.Path, dst: pathlib.Path) -> None:
    model = version_converter.convert_version(onnx.load(str(src)), 11)
    resizes = [n for n in model.graph.node if n.op_type == "Resize"]
    if len(resizes) != 5:
        sys.exit(f"esperava 5 Resize, achei {len(resizes)}")
    for i, node in enumerate(resizes):
        for a in list(node.attribute):
            if a.name == "coordinate_transformation_mode":
                node.attribute.remove(a)
        mode = "align_corners" if i < len(resizes) - 1 else "half_pixel"
        node.attribute.append(helper.make_attribute("coordinate_transformation_mode", mode))
    onnx.save(model, str(dst))


def fix_pnnx_pad(param: pathlib.Path) -> None:
    lines = param.read_bytes().decode("ascii").split("\n")
    fixed = 0
    for i, line in enumerate(lines):
        parts = line.split()
        if len(parts) > 1 and parts[1] in PNNX_FUSED_PAD_FIX:
            new = re.sub(r" 14=0 15=2 16=2 ", " 14=1 15=2 16=2 ", line)
            if new != line:
                lines[i] = new
                fixed += 1
    if fixed != len(PNNX_FUSED_PAD_FIX):
        sys.exit(f"correção do recuo do topo: esperava {len(PNNX_FUSED_PAD_FIX)}, fiz {fixed}")
    param.write_bytes("\n".join(lines).encode("ascii"))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("onnx")
    ap.add_argument("out")
    ap.add_argument("--pnnx", required=True)
    ap.add_argument("--ncnnoptimize", required=True)
    args = ap.parse_args()
    out = pathlib.Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    src = pathlib.Path(args.onnx).resolve()
    pnnx = str(pathlib.Path(shutil.which(args.pnnx) or args.pnnx).resolve())
    optimize = str(pathlib.Path(shutil.which(args.ncnnoptimize) or args.ncnnoptimize).resolve())
    print("onnx de origem", sha256(src), src.stat().st_size, "bytes")
    work = out / "midas_small_train.onnx"
    restore_resize(src, work)
    print("onnx com Resize do treino", sha256(work))
    subprocess.run([pnnx, work.name, "inputshape=[1,3,256,256]", "fp16=0", "device=cpu"],
                   cwd=out, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    param, weights = out / "midas_small_train.ncnn.param", out / "midas_small_train.ncnn.bin"
    fix_pnnx_pad(param)
    final_param, final_bin = out / "midas-v21-small-256.param", out / "midas-v21-small-256.bin"
    subprocess.run([optimize, str(param), str(weights), str(final_param), str(final_bin), "65536"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    for f in (final_param, final_bin):
        print(f.name, sha256(f), f.stat().st_size, "bytes")


if __name__ == "__main__":
    main()
