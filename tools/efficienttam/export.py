"""Trace each piece and convert with pnnx to ncnn, fp32 and fp16 weights. Usage: export.py [piece...]"""
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from common import *  # noqa

import numpy as np
import torch

import mods

PNNX = os.path.join(ROOT, "venv", "Scripts", "pnnx.exe")
M1, M2 = 2 * 1024 + 8, 7 * 1024 + 64
SHAPES = {
    "image_encoder": ([[1, 3, 512, 512]], None),
    "prompt_embed": ([[1, 2, 2], [1, 2, 5]], [[1, 3, 2], [1, 3, 5]]),
    "mask_prompt": ([[1, 1, 128, 128]], None),
    "mask_decoder": ([[1, 256, 32, 32], [1, 256, 32, 32], [1, 2, 256]],
                     [[1, 256, 32, 32], [1, 256, 32, 32], [1, 3, 256]]),
    "memory_encoder": ([[1, 256, 32, 32], [1, 1, 512, 512]], None),
    "memory_attention": ([[1, 256, 32, 32], [1, M1, 64], [1, M1, 64], [1, M1, 128], [1, M1, 128]],
                         [[1, 256, 32, 32], [1, M2, 64], [1, M2, 64], [1, M2, 128], [1, M2, 128]]),
}


def fmt(shapes):
    return ",".join("[" + ",".join(str(d) for d in s) + "]f32" for s in shapes)


pred = load_predictor()
nets, consts = mods.build_all(pred)
np.savez(os.path.join(MODELS if os.path.isdir(MODELS) else OUT, "constants.npz"), **consts)
pieces = sys.argv[1:] or list(SHAPES)
for name in pieces:
    shapes, shapes2 = SHAPES[name]
    d = os.path.join(MODELS, name)
    os.makedirs(d, exist_ok=True)
    pt = os.path.join(d, name + ".pt")
    args = tuple(torch.randn(*s) for s in shapes)
    with torch.no_grad():
        ts = torch.jit.trace(nets[name], args, check_trace=False)
    ts.save(pt)
    for tag, fp16 in (("fp32", 0), ("fp16", 1)):
        cmd = [PNNX, pt, "inputshape=" + fmt(shapes),
               "pnnxparam=" + os.path.join(d, "p.pnnx.param"), "pnnxbin=" + os.path.join(d, "p.pnnx.bin"),
               "pnnxpy=" + os.path.join(d, "p_pnnx.py"), "pnnxonnx=" + os.path.join(d, "p.pnnx.onnx"),
               "ncnnparam=" + os.path.join(d, tag + ".param"), "ncnnbin=" + os.path.join(d, tag + ".bin"),
               "ncnnpy=" + os.path.join(d, "p_ncnn.py"), "fp16=%d" % fp16]
        if shapes2:
            cmd.append("inputshape2=" + fmt(shapes2))
        r = subprocess.run(cmd, cwd=d, capture_output=True, text=True, errors="replace")
        log = (r.stdout or "") + (r.stderr or "")
        open(os.path.join(d, "pnnx_%s.log" % tag), "w", encoding="utf-8").write(log)
        ok = os.path.exists(os.path.join(d, tag + ".param"))
        bad = [l for l in log.splitlines() if any(w in l.lower() for w in ("unsupported", "ignore", "error", "fail"))]
        print(name, tag, "rc", r.returncode, "ok" if ok else "NO OUTPUT", "lines", len(log.splitlines()))
        for l in sorted(set(bad))[:25]:
            print("   ", l[:160])
