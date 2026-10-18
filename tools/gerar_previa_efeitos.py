"""Gera a foto de amostra das prévias de efeito do AUREA (previa_efeitos.jpg).

Imagem 100% procedural, criada por este script (sem foto, sem pessoa, nada
extraído de outro aplicativo): um vaso estilizado iluminado no centro, sobre
uma mesa de madeira, com uma esfera e uma pirâmide pequenas na frente e uma
parede escura ao fundo (cantos escuros).

Por que essa composição:
- Assunto central claro e "perto"; cantos de cima escuros e "longe": os testes
  de profundidade (engine/tests/test_ai.cpp, engine/tests/test_gpu.cpp) medem
  a região central (37,5%..62,5% x 37,5%..75%) contra os cantos de cima.
- Gradientes, faixas geométricas, brilho especular, sombras e textura fina
  (grão na parede, veios na mesa): efeitos de cor, borda, nitidez e desfoque
  ficam visíveis na prévia.

Saída determinística (semente fixa). Uso:
    python tools/gerar_previa_efeitos.py [saida.jpg ...]
Sem argumentos grava os dois arquivos oficiais (Android e iOS), byte a byte iguais.
"""
from __future__ import annotations

import hashlib
import io
import sys
from pathlib import Path

import numpy as np
from PIL import Image

N = 320          # lado final
SS = 2           # superamostragem (bordas suaves)
S = N * SS
SEED = 20261018

RAIZ = Path(__file__).resolve().parents[1]
SAIDAS = [
    RAIZ / "android/app/src/main/assets/previa_efeitos.jpg",
    RAIZ / "engine/platform/ios/app/previa_efeitos.jpg",
]


def smooth(e0: float, e1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix(a: np.ndarray, b, t: np.ndarray) -> np.ndarray:
    return a * (1.0 - t[..., None]) + np.asarray(b, dtype=np.float64) * t[..., None]


def value_noise(rng: np.random.Generator, cells: int) -> np.ndarray:
    """Ruído de valor suave (interpolação bicúbica simples), 0..1, S x S."""
    g = rng.random((cells + 1, cells + 1))
    u = np.linspace(0, cells, S, endpoint=False)
    i = u.astype(int)
    f = u - i
    f = f * f * (3 - 2 * f)
    a = g[i][:, i]
    b = g[i][:, i + 1]
    c = g[i + 1][:, i]
    d = g[i + 1][:, i + 1]
    fx = f[None, :]
    fy = f[:, None]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def render() -> np.ndarray:
    rng = np.random.default_rng(SEED)
    y, x = np.mgrid[0:S, 0:S].astype(np.float64)
    u = (x + 0.5) / S          # 0..1, esquerda -> direita
    v = (y + 0.5) / S          # 0..1, cima -> baixo

    # ---- Parede do fundo: azul-petróleo profundo, luz morna atrás do vaso,
    # vinheta forte (cantos quase pretos) e grão fino.
    wall_noise = 0.6 * value_noise(rng, 24) + 0.4 * value_noise(rng, 80)
    glow = np.exp(-(((u - 0.5) / 0.30) ** 2 + ((v - 0.42) / 0.30) ** 2))
    img = np.empty((S, S, 3))
    img[:] = (0.035, 0.05, 0.075)
    img = mix(img, (0.16, 0.20, 0.27), glow * 0.85)
    img *= (0.88 + 0.24 * wall_noise)[..., None]
    corner = np.sqrt((u - 0.5) ** 2 + (v - 0.45) ** 2)
    img *= (1.0 - 0.75 * smooth(0.30, 0.72, corner))[..., None]

    # ---- Mesa de madeira (do horizonte 0,70 até a base), em perspectiva:
    # mais clara e mais "perto" embaixo; veios que convergem.
    horizon = 0.70
    table = v > horizon
    depth = np.clip((v - horizon) / (1.0 - horizon), 1e-3, 1.0)   # 0 longe, 1 perto
    px = (u - 0.5) / (0.35 + depth)                                # convergência
    grain = (np.sin(px * 60.0 + 2.5 * value_noise(rng, 12) + 0.5 * np.sin(v * 25.0)) * 0.5 + 0.5)
    wood = np.empty((S, S, 3))
    wood[:] = (0.30, 0.17, 0.08)
    wood = mix(wood, (0.55, 0.33, 0.16), grain * 0.55 + 0.25 * value_noise(rng, 40))
    wood *= (0.45 + 0.75 * depth)[..., None]                       # luz cai ao fundo
    wood *= (1.0 - 0.55 * smooth(0.25, 0.62, np.abs(u - 0.5)))[..., None]
    edge = smooth(horizon, horizon + 0.012, v)                     # quina da mesa
    img = np.where(table[..., None], mix(img, wood, edge), img)
    lip = np.exp(-((v - horizon - 0.006) / 0.004) ** 2) * (1.0 - smooth(0.2, 0.5, np.abs(u - 0.5)))
    img = mix(img, (0.85, 0.62, 0.40), lip * 0.6)

    # ---- Sombras de contato na mesa (vaso, esfera, pirâmide).
    def shadow(cx, cy, rx, ry, k):
        d = ((u - cx) / rx) ** 2 + ((v - cy) / ry) ** 2
        return k * np.exp(-d * 2.2) * table

    sh = shadow(0.53, 0.815, 0.26, 0.045, 0.75) + shadow(0.21, 0.915, 0.10, 0.03, 0.7) + shadow(0.80, 0.93, 0.11, 0.03, 0.6)
    img *= (1.0 - np.clip(sh, 0, 0.85))[..., None]

    # ---- O vaso: corpo bojudo + gargalo + boca, perfil de revolução em u.
    cx = 0.5
    # meia-largura do perfil em função de v
    def half_width(vv):
        body = 0.245 * np.sqrt(np.clip(1.0 - ((vv - 0.585) / 0.225) ** 2, 0.0, 1.0))
        neck_t = smooth(0.40, 0.25, vv)                        # sobe para o gargalo
        neck = 0.075 + 0.030 * smooth(0.25, 0.17, vv)          # boca se abre
        w = np.maximum(body * (1.0 - 0.0 * neck_t), np.where((vv > 0.165) & (vv < 0.45), neck, 0.0))
        return np.where((vv >= 0.165) & (vv <= 0.80), w, 0.0)

    hw = half_width(v)
    inside = np.abs(u - cx) < hw
    aa = smooth(0.0, 2.0 / S, hw - np.abs(u - cx))             # borda suave
    # normal aproximada de um sólido de revolução: nx do perfil, ny pela curva
    nx = np.where(hw > 1e-6, (u - cx) / np.maximum(hw, 1e-6), 0.0)
    nz = np.sqrt(np.clip(1.0 - nx ** 2, 0.0, 1.0))
    dv = 1.0 / S
    dhw = (half_width(v + dv) - half_width(v - dv)) / (2 * dv)
    ny = np.clip(-dhw * 0.35, -0.8, 0.8)
    n = np.stack([nx, ny, nz], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True) + 1e-9
    light = np.array([-0.55, -0.55, 0.63]); light /= np.linalg.norm(light)
    rim_dir = np.array([0.85, -0.2, 0.3]); rim_dir /= np.linalg.norm(rim_dir)
    lam = np.clip(n @ light, 0, 1)
    rim = np.clip(n @ rim_dir, 0, 1) ** 4
    half = light + np.array([0, 0, 1.0]); half /= np.linalg.norm(half)
    spec = np.clip(n @ half, 0, 1) ** 60

    # cor do vaso: esmalte azul-turquesa que esquenta para baixo, com faixas
    base = mix(np.broadcast_to(np.array([0.05, 0.55, 0.62]), (S, S, 3)).copy(), (0.10, 0.28, 0.55), smooth(0.45, 0.80, v))
    # faixa dourada larga e duas finas
    gold = np.array([0.95, 0.72, 0.22])
    band = (smooth(0.0, 0.004, 0.012 - np.abs(v - 0.505)) + smooth(0.0, 0.003, 0.006 - np.abs(v - 0.69))
            + smooth(0.0, 0.003, 0.006 - np.abs(v - 0.45)))
    base = mix(base, gold, np.clip(band, 0, 1))
    # ziguezague magenta entre as faixas (na superfície: usa ângulo do perfil)
    ang = np.arcsin(np.clip(nx, -1, 1))                        # -pi/2..pi/2
    zz = 0.60 + 0.025 * np.abs(((ang * 6.0 / np.pi) % 2.0) - 1.0) * 2.0 - 0.025
    zig = smooth(0.0, 0.004, 0.009 - np.abs(v - zz)) * smooth(0.40, 0.75, nz)
    base = mix(base, (0.90, 0.18, 0.52), zig)
    # pontos laranja na faixa de cima do bojo
    dots = (np.sin(ang * 14.0) * 0.5 + 0.5) ** 6 * smooth(0.0, 0.004, 0.014 - np.abs(v - 0.555))
    base = mix(base, (1.0, 0.50, 0.12), np.clip(dots * 1.4, 0, 1))
    # boca escura por dentro
    mouth = smooth(0.0, 0.006, 0.010 - np.abs(v - 0.172)) * (np.abs(u - cx) < 0.09)
    glaze = 0.85 + 0.15 * value_noise(rng, 60)
    shade = (0.10 + 0.95 * lam)[..., None] * base * glaze[..., None]
    shade += (rim * 0.35)[..., None] * np.array([0.55, 0.75, 1.0])
    shade += (spec * 0.9)[..., None]
    shade = mix(shade, (0.02, 0.03, 0.04), mouth * 0.85)
    img = mix(img, np.clip(shade, 0, None), aa * inside)

    # ---- Esfera vermelha (frente, esquerda) e pirâmide amarela (frente, direita).
    def sphere(img, scx, scy, r, color):
        dx = (u - scx) / r
        dy = (v - scy) / r
        d2 = dx * dx + dy * dy
        m = smooth(1.0, 1.0 - 3.0 / (r * S), d2)
        nz2 = np.sqrt(np.clip(1 - d2, 0, 1))
        nn = np.stack([dx, dy, nz2], -1)
        l2 = np.clip(nn @ light, 0, 1)
        sp = np.clip(nn @ half, 0, 1) ** 40
        checker = ((np.floor((np.arctan2(dy, dx) + np.pi) / (np.pi / 4)) + np.floor(d2 * 3)) % 2)
        col = mix(np.broadcast_to(np.asarray(color), (S, S, 3)).copy(), (1.0, 0.95, 0.85), checker * 0.35)
        out = (0.12 + 0.95 * l2)[..., None] * col + (sp * 0.8)[..., None]
        return mix(img, out, m)

    img = sphere(img, 0.20, 0.86, 0.075, (0.85, 0.12, 0.10))

    # pirâmide: triângulo com duas faces (luz e sombra)
    apex = (0.80, 0.74)
    bl, br = (0.71, 0.935), (0.89, 0.935)
    mid = (0.835, 0.95)

    def tri_mask(p0, p1, p2):
        def side(a, b):
            return (b[0] - a[0]) * (v - a[1]) - (b[1] - a[1]) * (u - a[0])
        s0, s1, s2 = side(p0, p1), side(p1, p2), side(p2, p0)
        pos = (s0 >= 0) & (s1 >= 0) & (s2 >= 0)
        neg = (s0 <= 0) & (s1 <= 0) & (s2 <= 0)
        return (pos | neg).astype(np.float64)

    face_l = tri_mask(apex, bl, mid)
    face_r = tri_mask(apex, mid, br)
    tex = 0.9 + 0.1 * np.sin((u + v) * 260.0)
    img = mix(img, np.array([1.0, 0.82, 0.18]) * 0.95, face_l * 1.0)
    img = mix(img, np.array([0.70, 0.45, 0.06]) * 0.85, face_r * 1.0)
    img *= np.where((face_l + face_r) > 0, tex, 1.0)[..., None]

    # ---- Anéis finos de luz na parede (geometria, bordas nítidas para nitidez/borda).
    ring = np.sqrt(((u - 0.5) / 1.0) ** 2 + ((v - 0.40) / 1.0) ** 2)
    rings = smooth(0.0, 0.0025, 0.0035 - np.abs(ring - 0.40)) + smooth(0.0, 0.0025, 0.0030 - np.abs(ring - 0.335))
    rings *= (v < horizon) * (1.0 - inside) * (1.0 - smooth(0.30, 0.55, np.abs(u - 0.5)))
    img = mix(img, (0.45, 0.70, 0.95), np.clip(rings, 0, 1) * 0.45)

    # tom final: leve curva e grão de filme
    img = np.clip(img, 0, 1) ** (1 / 1.08)
    img += (rng.random((S, S)) - 0.5)[..., None] * 0.012
    return np.clip(img, 0, 1)


def encode() -> bytes:
    lin = render()
    big = Image.fromarray((lin * 255.0 + 0.5).astype(np.uint8), "RGB")
    small = big.resize((N, N), Image.Resampling.LANCZOS)
    buf = io.BytesIO()
    small.save(buf, "JPEG", quality=88, optimize=True, progressive=False, subsampling="4:2:0")
    return buf.getvalue()


def main(argv: list[str]) -> int:
    data = encode()
    outs = [Path(a) for a in argv] or SAIDAS
    for out in outs:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(data)
        print(f"{out}  {len(data)} bytes")
    print("sha256", hashlib.sha256(data).hexdigest())
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
