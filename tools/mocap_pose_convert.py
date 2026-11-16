"""Converte os modelos de pose do MediaPipe (BlazePose GHUM 3D, Apache-2.0) no
formato ncnn do motor (Motion Capture do Aurea PC).

Origem, tamanhos e hashes: build/reference/mocap/SOURCES.md e
engine/assets/mocap/README.md. Os bytes de engine/assets/mocap saem DESTA
receita, que é determinística:

    python -I tools/mocap_pose_convert.py <pose_detection.tflite> \
        <pose_landmark_full.tflite> <pasta de saída>

Sem pnnx/onnx no meio: o grafo do TFLite é lido direto (pacote `tflite`, só o
esquema flatbuffers) e cada operador vira a camada ncnn equivalente. Os pesos
do Google já vêm em fp16 (DEQUANTIZE) e vão em fp16 para o .bin do ncnn — a
conversão não perde nada. Só as saídas que o motor usa entram no grafo; o que
não leva a elas (a máscara de segmentação, o heatmap, os Reshape/Concat do
detector) fica de fora.

  TFLite (NHWC)           ncnn (CHW)
  CONV_2D                 Convolution (pad explícito; RELU/RELU6 fundidos)
  DEPTHWISE_CONV_2D       ConvolutionDepthWise (group = canais de entrada)
  PAD                     Padding (inclusive o de canais dos atalhos)
  ADD                     BinaryOp soma (+ Clip/ReLU fundidos)
  MAX_POOL_2D             Pooling máx
  RESIZE_BILINEAR         Interp bilinear: half_pixel e align_corners diretos;
                          o assimétrico ×2 vira align_corners para 2N-1 e uma
                          coluna/linha repetida (mesmo resultado, exato)
  DEPTH_TO_SPACE          PixelShuffle modo DCR
  LOGISTIC                Sigmoid
  DEQUANTIZE / DENSIFY    resolvidos aqui (constantes)

Saídas:
  pose_landmark_full: `lm` [195 = 39 × (x, y, z, visibilidade, presença)],
                      `flag` [1] (presença da pessoa, já com sigmoide),
                      `world` [117 = 39 × xyz em metros]
  pose_detection:     `cls8` [2,28,28] `reg8` [24,28,28] `cls16` [2,14,14]
                      `reg16` [24,14,14] `cls32` [6,7,7] `reg32` [72,7,7]
"""
import argparse
import hashlib
import pathlib
import struct
import sys

import numpy as np
import tflite

FP16_TAG = 0x01306B47
OPS = {v: k for k, v in tflite.BuiltinOperator.__dict__.items() if not k.startswith("_")}
NP_TYPE = {
    tflite.TensorType.FLOAT32: np.float32,
    tflite.TensorType.FLOAT16: np.float16,
    tflite.TensorType.INT32: np.int32,
    tflite.TensorType.UINT8: np.uint8,
    tflite.TensorType.INT8: np.int8,
}


def options(op, cls):
    table = op.BuiltinOptions()
    if table is None:
        return None
    o = cls()
    o.Init(table.Bytes, table.Pos)
    return o


def vec(dm_union_type, table):
    """Segmentos/índices de DimensionMetadata (vetor int32/uint16/uint8)."""
    if table is None:
        return []
    cls = {tflite.SparseIndexVector.Int32Vector: tflite.Int32Vector,
           tflite.SparseIndexVector.Uint16Vector: tflite.Uint16Vector,
           tflite.SparseIndexVector.Uint8Vector: tflite.Uint8Vector}[dm_union_type]
    v = cls()
    v.Init(table.Bytes, table.Pos)
    return [int(x) for x in v.ValuesAsNumpy()]


def densify(values, sparsity, dense_shape):
    """FormatConverter::SparseToDense do TFLite (mesma ordem de percurso)."""
    order = [int(x) for x in sparsity.TraversalOrderAsNumpy()]
    block_map = [int(x) for x in sparsity.BlockMapAsNumpy()] if sparsity.BlockMapLength() else []
    levels = []
    for i in range(sparsity.DimMetadataLength()):
        d = sparsity.DimMetadata(i)
        if d.Format() == tflite.DimensionType.DENSE:
            levels.append((0, d.DenseSize(), None, None))
        else:
            levels.append((1, 0, vec(d.ArraySegmentsType(), d.ArraySegments()),
                           vec(d.ArrayIndicesType(), d.ArrayIndices())))
    rank = len(dense_shape)
    block_size = [levels[order.index(rank + k)][1] for k in range(len(block_map))]
    out = np.zeros(int(np.prod(dense_shape)), dtype=values.dtype)
    flat = values.reshape(-1)
    src = [0]
    idx = [0] * len(order)
    strides = [int(np.prod(dense_shape[i + 1:])) for i in range(rank)]

    def populate(level, prev):
        if level == len(order):
            orig = [0] * rank
            for i in range(rank):
                orig[order[i]] = idx[i]
            for i in range(rank, len(order)):
                b = order[i] - rank
                orig[block_map[b]] = orig[block_map[b]] * block_size[b] + idx[i]
            out[sum(o * s for o, s in zip(orig, strides))] = flat[src[0]]
            src[0] += 1
            return
        fmt, size, seg, ind = levels[level]
        if fmt == 0:
            for i in range(size):
                idx[level] = i
                populate(level + 1, prev * size + i)
        elif prev + 1 < len(seg):
            for i in range(seg[prev], seg[prev + 1]):
                if i < len(ind):
                    idx[level] = ind[i]
                    populate(level + 1, i)

    sys.setrecursionlimit(10000)
    populate(0, 0)
    if src[0] != flat.size:
        sys.exit(f"densify: usou {src[0]} de {flat.size} valores")
    return out.reshape(dense_shape)


class Graph:
    def __init__(self, path):
        self.buf = pathlib.Path(path).read_bytes()
        self.model = tflite.Model.GetRootAsModel(self.buf, 0)
        if self.model.SubgraphsLength() != 1:
            sys.exit("esperava um subgrafo")
        self.g = self.model.Subgraphs(0)
        self.ops = [self.g.Operators(i) for i in range(self.g.OperatorsLength())]
        self.producer = {}
        for i, op in enumerate(self.ops):
            for k in range(op.OutputsLength()):
                self.producer[op.Outputs(k)] = i
        self.consts = {}
        self.input = self.g.Inputs(0)

    def tensor(self, t):
        return self.g.Tensors(t)

    def name(self, t):
        return self.tensor(t).Name().decode()

    def shape(self, t):
        return [int(x) for x in self.tensor(t).ShapeAsNumpy()]

    def opname(self, op):
        code = self.model.OperatorCodes(op.OpcodeIndex())
        return OPS[max(code.BuiltinCode(), code.DeprecatedBuiltinCode())]

    def const(self, t):
        """Valor constante do tensor (resolvendo DEQUANTIZE/DENSIFY) ou None."""
        if t in self.consts:
            return self.consts[t]
        tensor = self.tensor(t)
        value = None
        data = self.model.Buffers(tensor.Buffer()).DataAsNumpy() if tensor.Buffer() > 0 else 0
        if not isinstance(data, int) and len(data):
            raw = np.frombuffer(bytes(data), dtype=NP_TYPE[tensor.Type()])
            sp = tensor.Sparsity()
            if sp is not None:
                value = raw  # a forma densa vem do DENSIFY
                value = ("sparse", raw, sp)
            else:
                value = raw.reshape(self.shape(t)) if tensor.ShapeLength() else raw
        elif t in self.producer:
            op = self.ops[self.producer[t]]
            kind = self.opname(op)
            if kind == "DEQUANTIZE":
                src = self.const(op.Inputs(0))
                if src is not None and not isinstance(src, tuple):
                    value = src  # fp16 continua fp16: o .bin guarda a precisão de origem
            elif kind == "DENSIFY":
                src = self.const(op.Inputs(0))
                if isinstance(src, tuple):
                    value = densify(src[1], src[2], self.shape(t))
        self.consts[t] = value
        return value


class Writer:
    def __init__(self):
        self.layers = []   # (type, name, inputs, outputs, params)
        self.weights = bytearray()

    def layer(self, kind, name, inputs, outputs, params=""):
        self.layers.append([kind, name, list(inputs), list(outputs), params])

    def weight(self, array):
        """Pesos com etiqueta: fp16 fica fp16, o resto vai em fp32."""
        if array.dtype == np.float16:
            data = array.astype("<f2").tobytes()
            self.weights += struct.pack("<I", FP16_TAG) + data + b"\0" * ((4 - len(data) % 4) % 4)
        else:
            self.weights += struct.pack("<I", 0) + array.astype("<f4").tobytes()

    def raw(self, array):
        self.weights += array.astype("<f4").tobytes()

    def finish(self, param_path, bin_path):
        # Blob consumido por mais de uma camada: Split (exigência do ncnn).
        uses = {}
        for kind, name, ins, outs, _ in self.layers:
            for b in ins:
                uses.setdefault(b, []).append(name)
        final = []
        for layer in self.layers:
            kind, name, ins, outs, params = layer
            renamed = []
            for b in ins:
                if b in self.split_map:
                    renamed.append(self.split_map[b].pop(0))
                else:
                    renamed.append(b)
            layer[2] = renamed
            final.append(layer)
            for b in outs:
                if len(uses.get(b, [])) > 1:
                    n = len(uses[b])
                    names = [f"{b}_s{i}" for i in range(n)]
                    self.split_map[b] = list(names)
                    final.append(["Split", f"split_{b}", [b], names, ""])
        blobs = set()
        for _, _, ins, outs, _ in final:
            blobs.update(ins)
            blobs.update(outs)
        lines = ["7767517", f"{len(final)} {len(blobs)}"]
        for kind, name, ins, outs, params in final:
            lines.append(f"{kind:<24} {name:<24} {len(ins)} {len(outs)} {' '.join(ins + outs)}"
                         + (f" {params}" if params else ""))
        pathlib.Path(param_path).write_bytes(("\n".join(lines) + "\n").encode("ascii"))
        pathlib.Path(bin_path).write_bytes(bytes(self.weights))

    split_map = {}


def act_params(fused):
    if fused == tflite.ActivationFunctionType.NONE:
        return ""
    if fused == tflite.ActivationFunctionType.RELU:
        return " 9=1"
    if fused == tflite.ActivationFunctionType.RELU6:
        return " 9=3 -23310=2,0.000000e+00,6.000000e+00"
    sys.exit(f"ativação fundida {fused} não suportada")


def same_pads(size, k, s, d=1):
    out = -(-size // s)
    total = max((out - 1) * s + (k - 1) * d + 1 - size, 0)
    return total // 2, total - total // 2


def convert(path, outputs, out_param, out_bin, input_blob="in0"):
    G = Graph(path)
    W = Writer()
    W.split_map = {}
    blob = {G.input: input_blob}
    names = {}   # tensor → nome de saída pedido
    want = {}
    for tname, bname in outputs.items():
        hit = [t for t in range(G.g.TensorsLength()) if G.name(t) == tname]
        if len(hit) != 1:
            sys.exit(f"tensor de saída não achado: {tname}")
        want[hit[0]] = bname
    # Só os operadores que levam às saídas pedidas.
    needed = set()
    stack = list(want)
    while stack:
        t = stack.pop()
        if t == G.input or G.const(t) is not None or t not in G.producer:
            continue
        i = G.producer[t]
        if i in needed:
            continue
        needed.add(i)
        op = G.ops[i]
        stack.extend(op.Inputs(k) for k in range(op.InputsLength()) if op.Inputs(k) >= 0)
    ih, iw, ic = G.shape(G.input)[1:]
    W.layer("Input", "input", [], [input_blob], f"0={iw} 1={ih} 2={ic}")

    def name_of(t):
        if t in want:
            return want[t]
        if t not in names:
            names[t] = f"t{t}"
        return names[t]

    counter = [0]

    def lname(kind):
        counter[0] += 1
        return f"{kind}_{counter[0]}"

    for i, op in enumerate(G.ops):
        if i not in needed:
            continue
        kind = G.opname(op)
        x = op.Inputs(0)
        y = op.Outputs(0)
        src = blob[x]
        dst = name_of(y)
        xs = G.shape(x)
        ys = G.shape(y)
        if kind in ("CONV_2D", "DEPTHWISE_CONV_2D"):
            w = G.const(op.Inputs(1))
            b = G.const(op.Inputs(2)) if op.InputsLength() > 2 and op.Inputs(2) >= 0 else None
            if w is None:
                sys.exit(f"pesos não constantes em {kind} {i}")
            if kind == "CONV_2D":
                o = options(op, tflite.Conv2DOptions)
                outc, kh, kw, inc = w.shape
                ww = np.ascontiguousarray(w.transpose(0, 3, 1, 2))   # O,KH,KW,I → O,I,KH,KW
                group = 1
            else:
                o = options(op, tflite.DepthwiseConv2DOptions)
                _, kh, kw, outc = w.shape
                ww = np.ascontiguousarray(w[0].transpose(2, 0, 1))    # KH,KW,C·M → C·M,KH,KW
                group = xs[3]
            sh, sw = o.StrideH(), o.StrideW()
            dh, dw = o.DilationHFactor(), o.DilationWFactor()
            if o.Padding() == tflite.Padding.SAME:
                pt, pb = same_pads(xs[1], kh, sh, dh)
                pl, pr = same_pads(xs[2], kw, sw, dw)
            else:
                pt = pb = pl = pr = 0
            p = (f"0={outc} 1={kw} 11={kh} 2={dw} 12={dh} 3={sw} 13={sh} 4={pl} 15={pr} 14={pt} 16={pb}"
                 f" 5={1 if b is not None else 0} 6={ww.size}")
            if group != 1:
                p += f" 7={group}"
            p += act_params(o.FusedActivationFunction())
            W.layer("Convolution" if group == 1 else "ConvolutionDepthWise", lname("conv"), [src], [dst], p)
            W.weight(ww)
            if b is not None:
                W.raw(b.astype(np.float32))
        elif kind == "PAD":
            pads = G.const(op.Inputs(1))
            (n0, n1), (t, bt), (l, r), (cf, cb) = pads.tolist()
            if n0 or n1:
                sys.exit("PAD no lote não suportado")
            W.layer("Padding", lname("pad"), [src], [dst], f"0={t} 1={bt} 2={l} 3={r} 4=0 5=0.000000e+00 7={cf} 8={cb}")
        elif kind == "ADD":
            other = op.Inputs(1)
            if G.const(other) is not None or G.shape(other) != xs:
                sys.exit(f"ADD {i} com constante/broadcast não suportado")
            o = options(op, tflite.AddOptions)
            fused = o.FusedActivationFunction() if o else 0
            mid = dst if fused == 0 else f"{dst}_pre"
            W.layer("BinaryOp", lname("add"), [src, blob[other]], [mid], "0=0")
            if fused == tflite.ActivationFunctionType.RELU6:
                W.layer("Clip", lname("clip"), [mid], [dst], "0=0.000000e+00 1=6.000000e+00")
            elif fused == tflite.ActivationFunctionType.RELU:
                W.layer("ReLU", lname("relu"), [mid], [dst])
            elif fused != 0:
                sys.exit(f"ADD {i}: ativação {fused}")
        elif kind == "MAX_POOL_2D":
            o = options(op, tflite.Pool2DOptions)
            fh, fw, sh, sw = o.FilterHeight(), o.FilterWidth(), o.StrideH(), o.StrideW()
            if o.Padding() == tflite.Padding.SAME:
                pt, pb = same_pads(xs[1], fh, sh)
                pl, pr = same_pads(xs[2], fw, sw)
            else:
                pt = pb = pl = pr = 0
            if o.FusedActivationFunction() != 0:
                sys.exit("MAX_POOL com ativação")
            W.layer("Pooling", lname("pool"), [src], [dst],
                    f"0=0 1={fw} 11={fh} 2={sw} 12={sh} 3={pl} 14={pr} 13={pt} 15={pb} 5=1")
        elif kind == "RESIZE_BILINEAR":
            o = options(op, tflite.ResizeBilinearOptions)
            oh, ow = ys[1], ys[2]
            if o.HalfPixelCenters():
                W.layer("Interp", lname("interp"), [src], [dst], f"0=2 3={oh} 4={ow} 6=0")
            elif o.AlignCorners():
                W.layer("Interp", lname("interp"), [src], [dst], f"0=2 3={oh} 4={ow} 6=1")
            else:
                # Assimétrico (src = dst/2): align_corners até 2N-1 dá exatamente
                # src = dst/2; o último ponto (src = N-0,5) é o da borda repetido.
                if oh != 2 * xs[1] or ow != 2 * xs[2]:
                    sys.exit("RESIZE assimétrico só ×2")
                mid = f"{dst}_ac"
                W.layer("Interp", lname("interp"), [src], [mid], f"0=2 3={oh - 1} 4={ow - 1} 6=1")
                W.layer("Padding", lname("pad"), [mid], [dst], "0=0 1=1 2=0 3=1 4=1")
        elif kind == "DEPTH_TO_SPACE":
            o = options(op, tflite.DepthToSpaceOptions)
            W.layer("PixelShuffle", lname("d2s"), [src], [dst], f"0={o.BlockSize()} 1=1")
        elif kind == "LOGISTIC":
            W.layer("Sigmoid", lname("sigmoid"), [src], [dst])
        else:
            sys.exit(f"operador não suportado: {kind} ({i})")
        blob[y] = dst
    W.finish(out_param, out_bin)


LANDMARK_OUTPUTS = {
    "model_1/model/convld_3d/BiasAdd;model_1/model/convld_3d/Conv2D;model_1/model/convld_3d/BiasAdd/ReadVariableOp/resource1": "lm",
    "model_1/model/activation_poseflag/Sigmoid": "flag",
    "model_1/model/convworld_3d/BiasAdd;model_1/model/convworld_3d/Conv2D;model_1/model/convworld_3d/BiasAdd/ReadVariableOp/resource1": "world",
}


def detector_outputs(path):
    """As seis cabeças do detector, antes dos Reshape (classificador/regressor por passo)."""
    G = Graph(path)
    out = {}
    for op in G.ops:
        if G.opname(op) != "RESHAPE":
            continue
        t = op.Inputs(0)
        n = G.name(t)
        for stride in (8, 16, 32):
            for kind, short in (("classifier", "cls"), ("regressor", "reg")):
                if f"{kind}_person_{stride}_NO_PRUNING/BiasAdd;" in n or n.startswith(f"model_1/model/{kind}_person_{stride}_NO_PRUNING/BiasAdd"):
                    out[n] = f"{short}{stride}"
    if sorted(out.values()) != sorted(["cls8", "reg8", "cls16", "reg16", "cls32", "reg32"]):
        sys.exit(f"cabeças do detector: {sorted(out.values())}")
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("detector")
    ap.add_argument("landmark", nargs="+",
                    help="pose_landmark_full.tflite e/ou pose_landmark_heavy.tflite (mesmo grafo, mesmos nomes)")
    ap.add_argument("out")
    a = ap.parse_args()
    out = pathlib.Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    for path in a.landmark:
        # O nome de saída segue o arquivo de origem: pose_landmark_full / pose_landmark_heavy.
        stem = pathlib.Path(path).stem
        if stem not in ("pose_landmark_full", "pose_landmark_heavy", "pose_landmark_lite"):
            sys.exit(f"modelo de landmarks inesperado: {stem}")
        outputs = dict(LANDMARK_OUTPUTS)
        # O heatmap (64×64×39) só vai no pesado: é ele que refina x/y (RefineLandmarksFromHeatmap).
        if stem == "pose_landmark_heavy":
            outputs["Identity_3"] = "heatmap"
        convert(path, outputs, out / f"{stem}.param", out / f"{stem}.bin")
    convert(a.detector, detector_outputs(a.detector), out / "pose_detection.param", out / "pose_detection.bin")
    for f in sorted(out.glob("pose_*")):
        print(f"{f.name}  {f.stat().st_size}  {hashlib.sha256(f.read_bytes()).hexdigest()}")


if __name__ == "__main__":
    main()
