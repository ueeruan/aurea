"""AureaHyper (0.3.2): baixa, converte e quantiza as redes Lite/Max.

Receita reproduzível dos arquivos em engine/assets/hyper/<modelo>/ (embutidos
no app) e dos modelos "sob demanda" (grandes demais para o APK, gravados fora
do repositório em build/hyper-tools/out/ondemand/<modelo>/).

    build/hyper-tools/venv/Scripts/python -I tools/hyper_models.py [--only a,b] [--tools build/hyper-tools]

Ferramentas (nunca dentro do repositório):
  * venv em build/hyper-tools/venv: torch (CPU), pnnx==20250430, numpy;
  * ncnnoptimize / ncnn2table / ncnn2int8 compilados da árvore ncnn FIXADA do
    motor (305837fd…, a mesma de engine/cmake/AureaNcnn.cmake) em
    build/hyper-tools/ncnn-tools (CPU, NCNN_INT8=ON).

Regras de segurança (tudo o que se baixa é dado não confiável):
  * só URLs oficiais, cada arquivo com SHA-256 fixado aqui; hash diferente = erro;
  * cada download numa pasta nova e vazia (dl/<nome>/), nada é executado de lá;
  * checkpoints PyTorch abertos com torch.load(weights_only=True) (sem pickle
    arbitrário); arquiteturas escritas AQUI (nenhum .py baixado é importado);
  * o .zip do SPAN só tem os dois .pth lidos (zipfile, sem extrair o resto).

Calibração int8: imagens SINTÉTICAS geradas aqui (gradientes, grades, bordas,
ruído, blocos 8×8 de compressão) — nenhuma mídia de terceiros.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import pathlib
import shutil
import struct
import subprocess
import sys
import urllib.request
import zipfile
import zlib

import numpy as np

REPO = pathlib.Path(__file__).resolve().parents[1]
ASSETS = REPO / "engine" / "assets" / "hyper"

# ---------------------------------------------------------------------------
# Fontes oficiais (SHA-256 fixado no primeiro download, 25/10/2026)
# ---------------------------------------------------------------------------
SOURCES = {
    "realesr-general-x4v3.pth": dict(
        url="https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-x4v3.pth",
        sha256="8dc7edb9ac80ccdc30c3a5dca6616509367f05fbc184ad95b731f05bece96292", size=4885111),
    "realesr-general-wdn-x4v3.pth": dict(
        url="https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesr-general-wdn-x4v3.pth",
        sha256="1641f8c4464b9f097c9fdda5589273713f67cf59f3d909e0bd688f0cee269dca", size=4885111),
    "Real-ESRGAN-LICENSE": dict(
        url="https://raw.githubusercontent.com/xinntao/Real-ESRGAN/v0.3.0/LICENSE",
        sha256="4a699ec4863d96a91fc265948a0c90033f7e8735d515524dcf3444736406e0c2", size=1519),
    # SPAN: o README oficial (github.com/hongyuanyu/SPAN, "Release checkpoints")
    # aponta este arquivo do Google Drive dos autores; não há release no GitHub.
    "span-checkpoints.zip": dict(
        url="https://drive.usercontent.google.com/download?id=1iYUA2TzKuxI0vzmA-UXr_nB43XgPOXUg&export=download&confirm=t",
        sha256="675b653c5e730e149bb75864e04e4f3440fe8eb46e1c6f44d21b973fb5f747dc", size=1326754357),
    "SPAN-LICENSE": dict(
        url="https://raw.githubusercontent.com/hongyuanyu/SPAN/main/LICENSE.txt",
        sha256="5447872f21ddc29dc0dc060d5c885513c01c904e9da9931bba69898a6fb4d415", size=11350),
    # RIFE v4.6 já em ncnn, do repositório oficial rife-ncnn-vulkan (nihui), tag 20221029.
    "rife-v4.6-flownet.param": dict(
        url="https://raw.githubusercontent.com/nihui/rife-ncnn-vulkan/20221029/models/rife-v4.6/flownet.param",
        sha256="724569596bcd1e7b9fa50455c604777ebed99746d2ef40aa86e31b5725f1053c", size=16532),
    "rife-v4.6-flownet.bin": dict(
        url="https://raw.githubusercontent.com/nihui/rife-ncnn-vulkan/20221029/models/rife-v4.6/flownet.bin",
        sha256="f334ed2260149ce0188a6dcf049844e8b0cdd912e01cbcfb63553157d2508958", size=10614320),
    "rife-ncnn-vulkan-LICENSE": dict(
        url="https://raw.githubusercontent.com/nihui/rife-ncnn-vulkan/20221029/LICENSE",
        sha256="a73beab18143600af0b10c6050a953ec233775ce31c2bf3373a794db535329fd", size=1072),
    "Practical-RIFE-LICENSE": dict(
        url="https://raw.githubusercontent.com/hzwer/Practical-RIFE/main/LICENSE",
        sha256="7932fb49341512b959b1744a6d9cbb39e5a1ec89da438a34d0454d5d8df9fecd", size=1062),
    "GFPGANv1.4.pth": dict(
        url="https://github.com/TencentARC/GFPGAN/releases/download/v1.3.4/GFPGANv1.4.pth",
        sha256="e2cd4703ab14f4d01fd1383a8a8b266f9a5833dacee8e6a79d3bf21a1b6be5ad", size=348632874),
    "GFPGAN-LICENSE": dict(
        url="https://raw.githubusercontent.com/TencentARC/GFPGAN/v1.3.4/LICENSE",
        sha256="b916d19bf894eb43409866adf17c9f71cbb17aefd9cd105117c4a32598ef6a46", size=22990),
}


def sha256_file(path: pathlib.Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch(tools: pathlib.Path, name: str) -> pathlib.Path:
    """Baixa (uma vez) numa pasta própria e confere o SHA-256 fixado."""
    src = SOURCES[name]
    folder = tools / "dl" / name.replace(".", "_")
    target = folder / name
    if target.exists():
        digest = sha256_file(target)
        if src["sha256"] in (None, digest):
            return target
        sys.exit(f"{name}: SHA-256 {digest} difere do fixado {src['sha256']}")
    if folder.exists() and any(folder.iterdir()):
        sys.exit(f"{folder} não está vazia; apague antes de baixar de novo")
    folder.mkdir(parents=True, exist_ok=True)
    part = folder / (name + ".part")
    print(f"baixando {name} de {src['url']}")
    with urllib.request.urlopen(src["url"]) as r, part.open("wb") as f:
        shutil.copyfileobj(r, f, 1 << 20)
    digest = sha256_file(part)
    if src["sha256"] is not None and digest != src["sha256"]:
        part.unlink()
        sys.exit(f"{name}: SHA-256 {digest} difere do fixado {src['sha256']}")
    if src["sha256"] is None:
        print(f"  (licença sem hash fixado; SHA-256 {digest})")
    part.rename(target)
    return target


# ---------------------------------------------------------------------------
# Arquiteturas (escritas aqui a partir das publicações/código oficiais; só
# carregam os state_dict, nenhuma linha de código baixado é executada)
# ---------------------------------------------------------------------------
def torch_modules():
    import torch
    from torch import nn
    import torch.nn.functional as F

    class SRVGGNetCompact(nn.Module):
        """Real-ESRGAN SRVGGNetCompact (x4v3 / wdn-x4v3): 64 canais, 32 conv, PReLU."""

        def __init__(self, num_feat=64, num_conv=32, upscale=4):
            super().__init__()
            self.upscale = upscale
            body = [nn.Conv2d(3, num_feat, 3, 1, 1), nn.PReLU(num_feat)]
            for _ in range(num_conv):
                body += [nn.Conv2d(num_feat, num_feat, 3, 1, 1), nn.PReLU(num_feat)]
            body.append(nn.Conv2d(num_feat, 3 * upscale * upscale, 3, 1, 1))
            self.body = nn.ModuleList(body)
            self.upsampler = nn.PixelShuffle(upscale)

        def forward(self, x):
            out = x
            for layer in self.body:
                out = layer(out)
            out = self.upsampler(out)
            return out + F.interpolate(x, scale_factor=self.upscale, mode="nearest")

    def reparam_conv3xc(sd, prefix):
        """Conv3XC do SPAN em avaliação = UMA 3×3 (1×1→3×3→1×1 + atalho 1×1).
        Mesma álgebra de Conv3XC.update_params do artigo/código oficial."""
        w1, b1 = sd[prefix + "conv.0.weight"], sd[prefix + "conv.0.bias"]
        w2, b2 = sd[prefix + "conv.1.weight"], sd[prefix + "conv.1.bias"]
        w3, b3 = sd[prefix + "conv.2.weight"], sd[prefix + "conv.2.bias"]
        w = F.conv2d(w1.flip(2, 3).permute(1, 0, 2, 3), w2, padding=2, stride=1).flip(2, 3).permute(1, 0, 2, 3)
        b = (w2 * b1.reshape(1, -1, 1, 1)).sum((1, 2, 3)) + b2
        wc = F.conv2d(w.flip(2, 3).permute(1, 0, 2, 3), w3, padding=0, stride=1).flip(2, 3).permute(1, 0, 2, 3)
        bc = (w3 * b.reshape(1, -1, 1, 1)).sum((1, 2, 3)) + b3
        sk_w = F.pad(sd[prefix + "sk.weight"], [1, 1, 1, 1])
        return wc + sk_w, bc + sd[prefix + "sk.bias"]

    class SPANEval(nn.Module):
        """SPAN (CVPR-W 2024) já re-parametrizado: cada Conv3XC é uma 3×3."""

        def __init__(self, sd, upscale, ch=48):
            super().__init__()
            self.register_buffer("mean", torch.tensor([0.4488, 0.4371, 0.4040]).view(1, 3, 1, 1))

            def c3(prefix, cin, cout):
                conv = nn.Conv2d(cin, cout, 3, 1, 1)
                w, b = reparam_conv3xc(sd, prefix)
                conv.weight.data.copy_(w)
                conv.bias.data.copy_(b)
                return conv

            self.conv_1 = c3("conv_1.", 3, ch)
            self.blocks = nn.ModuleList()
            for i in range(1, 7):
                p = f"block_{i}."
                self.blocks.append(nn.ModuleList([c3(p + "c1_r.", ch, ch), c3(p + "c2_r.", ch, ch), c3(p + "c3_r.", ch, ch)]))
            self.conv_2 = c3("conv_2.", ch, ch)
            self.conv_cat = nn.Conv2d(ch * 4, ch, 1)
            self.conv_cat.weight.data.copy_(sd["conv_cat.weight"])
            self.conv_cat.bias.data.copy_(sd["conv_cat.bias"])
            self.up = nn.Conv2d(ch, 3 * upscale * upscale, 3, 1, 1)
            self.up.weight.data.copy_(sd["upsampler.0.weight"])
            self.up.bias.data.copy_(sd["upsampler.0.bias"])
            self.shuffle = nn.PixelShuffle(upscale)

        def block(self, convs, x):
            out1 = convs[0](x)
            out2 = convs[1](F.silu(out1))
            out3 = convs[2](F.silu(out2))
            att = torch.sigmoid(out3) - 0.5
            return (out3 + x) * att, out1

        def forward(self, x):
            x = (x - self.mean) * 255.0
            feat = self.conv_1(x)
            b1, _ = self.block(self.blocks[0], feat)
            b = b1
            for k in range(1, 5):
                b, _ = self.block(self.blocks[k], b)
            b6, b5_2 = self.block(self.blocks[5], b)
            b6 = self.conv_2(b6)
            out = self.conv_cat(torch.cat([feat, b6, b1, b5_2], 1))
            return self.shuffle(self.up(out))

    class SPANTrainGraph(nn.Module):
        """O SPAN com os ramos de treino (para conferir a re-parametrização)."""

        def __init__(self, sd, upscale, ch=48):
            super().__init__()
            self.sd, self.upscale = sd, upscale

        def c3(self, p, x):
            sd = self.sd
            y = F.conv2d(F.pad(x, (1, 1, 1, 1)), sd[p + "conv.0.weight"], sd[p + "conv.0.bias"])
            y = F.conv2d(y, sd[p + "conv.1.weight"], sd[p + "conv.1.bias"])
            y = F.conv2d(y, sd[p + "conv.2.weight"], sd[p + "conv.2.bias"])
            return y + F.conv2d(x, sd[p + "sk.weight"], sd[p + "sk.bias"])

        def block(self, i, x):
            p = f"block_{i}."
            o1 = self.c3(p + "c1_r.", x)
            o2 = self.c3(p + "c2_r.", F.silu(o1))
            o3 = self.c3(p + "c3_r.", F.silu(o2))
            att = torch.sigmoid(o3) - 0.5
            return (o3 + x) * att, o1

        def forward(self, x):
            mean = torch.tensor([0.4488, 0.4371, 0.4040]).view(1, 3, 1, 1)
            x = (x - mean) * 255.0
            feat = self.c3("conv_1.", x)
            b1, _ = self.block(1, feat)
            b = b1
            for i in range(2, 6):
                b, _ = self.block(i, b)
            b6, b5_2 = self.block(6, b)
            b6 = self.c3("conv_2.", b6)
            out = F.conv2d(torch.cat([feat, b6, b1, b5_2], 1), self.sd["conv_cat.weight"], self.sd["conv_cat.bias"])
            out = F.conv2d(out, self.sd["upsampler.0.weight"], self.sd["upsampler.0.bias"], padding=1)
            return F.pixel_shuffle(out, self.upscale)

    class GFPGANStatic(nn.Module):
        """GFPGANv1Clean 512 (v1.4: channel_multiplier=2, different_w, sft_half,
        input_is_latent) reescrito SEM pesos dinâmicos, para o ncnn.

        A convolução modulada do StyleGAN2 (peso·estilo, demodulação) é
        reescrita na forma equivalente "não fundida" do artigo do StyleGAN2:
            conv(x·s, W) · rsqrt(Σ_i s_i²·Σ_k W[o,i,k]² + ε)
        — mesma conta, só que o peso fica constante (o ncnn precisa disso).
        Ruído: os buffers fixos do checkpoint (randomize_noise=False), para o
        resultado ser determinístico.
        """

        def __init__(self, sd):
            super().__init__()
            self.sd = {k: v.float() for k, v in sd.items()}
            for k, v in self.sd.items():
                self.register_buffer(k.replace(".", "__"), v.clone(), persistent=False)
            self.log_size = 9
            # W2[o,i] = Σ_k W[o,i,k]² de cada conv modulada demodulada
            for k in list(self.sd.keys()):
                if k.endswith("modulated_conv.weight") and ".to_rgb" not in k:
                    w = self.sd[k][0]
                    self.register_buffer(k.replace(".", "__") + "__sq", (w * w).sum((2, 3)), persistent=False)

        def t(self, k):
            return getattr(self, k.replace(".", "__"))

        def resblock(self, p, x, factor):
            out = F.leaky_relu(F.conv2d(x, self.t(p + "conv1.weight"), self.t(p + "conv1.bias"), padding=1), 0.2)
            out = F.interpolate(out, scale_factor=factor, mode="bilinear", align_corners=False)
            out = F.leaky_relu(F.conv2d(out, self.t(p + "conv2.weight"), self.t(p + "conv2.bias"), padding=1), 0.2)
            x = F.interpolate(x, scale_factor=factor, mode="bilinear", align_corners=False)
            return out + F.conv2d(x, self.t(p + "skip.weight"))

        def modconv(self, p, x, w, demod, upsample, gain=1.0):
            s = F.linear(w, self.t(p + "modulation.weight"), self.t(p + "modulation.bias"))  # [1, cin]
            x = x * s.view(1, -1, 1, 1)
            if upsample:
                x = F.interpolate(x, scale_factor=2, mode="bilinear", align_corners=False)
            weight = self.t(p + "weight")[0]
            out = F.conv2d(x, weight, padding=weight.shape[-1] // 2)
            if demod:
                sq = getattr(self, (p + "weight").replace(".", "__") + "__sq")
                # o ×√2 do StyleConv entra aqui, no vetor [cout], e não numa soma
                # ponderada com o ruído: o pnnx funde "a·√2 + ruído" num Eltwise
                # de pesos, e o Eltwise do ncnn não faz broadcast ([C,h,w] + [1,h,w]).
                # Em escala 1/64: Σ s²·W² chega a 1,6e6 e estoura o fp16 (65504)
                # do armazenamento no Vulkan; (s/64)² mantém < 400. Mesma conta.
                sk = s * (1.0 / 64.0)
                d = torch.rsqrt(F.linear(sk * sk, sq) + 1e-8 / 4096.0) * (gain / 64.0)
                out = out * d.view(1, -1, 1, 1)
            return out

        def styleconv(self, p, x, w, noise, upsample):
            out = self.modconv(p + "modulated_conv.", x, w, True, upsample, gain=2 ** 0.5)
            out = out + self.t(p + "weight") * noise
            out = out + self.t(p + "bias")
            return F.leaky_relu(out, 0.2)

        def torgb(self, p, x, w, skip):
            out = self.modconv(p + "modulated_conv.", x, w, False, False) + self.t(p + "bias")
            if skip is not None:
                out = out + F.interpolate(skip, scale_factor=2, mode="bilinear", align_corners=False)
            return out

        def forward(self, x):
            conditions, skips = [], []
            feat = F.leaky_relu(F.conv2d(x, self.t("conv_body_first.weight"), self.t("conv_body_first.bias")), 0.2)
            for i in range(self.log_size - 2):
                feat = self.resblock(f"conv_body_down.{i}.", feat, 0.5)
                skips.insert(0, feat)
            feat = F.leaky_relu(F.conv2d(feat, self.t("final_conv.weight"), self.t("final_conv.bias"), padding=1), 0.2)
            style = F.linear(feat.reshape(1, -1), self.t("final_linear.weight"), self.t("final_linear.bias"))
            latent = style.view(1, 16, 512)
            for i in range(self.log_size - 2):
                feat = feat + skips[i]
                feat = self.resblock(f"conv_body_up.{i}.", feat, 2)
                p = f"condition_scale.{i}."
                sc = F.conv2d(F.leaky_relu(F.conv2d(feat, self.t(p + "0.weight"), self.t(p + "0.bias"), padding=1), 0.2),
                              self.t(p + "2.weight"), self.t(p + "2.bias"), padding=1)
                p = f"condition_shift.{i}."
                sh = F.conv2d(F.leaky_relu(F.conv2d(feat, self.t(p + "0.weight"), self.t(p + "0.bias"), padding=1), 0.2),
                              self.t(p + "2.weight"), self.t(p + "2.bias"), padding=1)
                conditions += [sc, sh]
            d = "stylegan_decoder."
            out = self.t(d + "constant_input.weight")
            out = self.styleconv(d + "style_conv1.", out, latent[:, 0], self.t(d + "noises.noise0"), False)
            skip = self.torgb(d + "to_rgb1.", out, latent[:, 1], None)
            i = 1
            for k in range(7):
                out = self.styleconv(d + f"style_convs.{2 * k}.", out, latent[:, i], self.t(d + f"noises.noise{2 * k + 1}"), True)
                if i < len(conditions):
                    half = out.shape[1] // 2
                    same, sft = out[:, :half], out[:, half:]
                    sft = sft * conditions[i - 1] + conditions[i]
                    out = torch.cat([same, sft], 1)
                out = self.styleconv(d + f"style_convs.{2 * k + 1}.", out, latent[:, i + 1], self.t(d + f"noises.noise{2 * k + 2}"), False)
                skip = self.torgb(d + f"to_rgbs.{k}.", out, latent[:, i + 2], skip)
                i += 2
            return skip

    def gfpgan_reference(sd, x):
        """A conta ORIGINAL (peso modulado por amostra, conv agrupada), transcrita
        do GFPGANv1Clean/StyleGAN2GeneratorClean oficiais, só para conferir."""
        sd = {k: v.float() for k, v in sd.items()}

        def modconv(p, x, w, demod, upsample):
            b, c = x.shape[:2]
            style = F.linear(w, sd[p + "modulation.weight"], sd[p + "modulation.bias"]).view(b, 1, c, 1, 1)
            weight = sd[p + "weight"] * style
            if demod:
                dm = torch.rsqrt(weight.pow(2).sum([2, 3, 4]) + 1e-8)
                weight = weight * dm.view(b, -1, 1, 1, 1)
            k = weight.shape[-1]
            weight = weight.view(-1, c, k, k)
            if upsample:
                x = F.interpolate(x, scale_factor=2, mode="bilinear", align_corners=False)
            return F.conv2d(x, weight, padding=k // 2, groups=b)

        def styleconv(p, x, w, noise, up):
            out = modconv(p + "modulated_conv.", x, w, True, up) * 2 ** 0.5
            return F.leaky_relu(out + sd[p + "weight"] * noise + sd[p + "bias"], 0.2)

        def torgb(p, x, w, skip):
            out = modconv(p + "modulated_conv.", x, w, False, False) + sd[p + "bias"]
            if skip is not None:
                out = out + F.interpolate(skip, scale_factor=2, mode="bilinear", align_corners=False)
            return out

        def res(p, x, f):
            out = F.leaky_relu(F.conv2d(x, sd[p + "conv1.weight"], sd[p + "conv1.bias"], padding=1), 0.2)
            out = F.interpolate(out, scale_factor=f, mode="bilinear", align_corners=False)
            out = F.leaky_relu(F.conv2d(out, sd[p + "conv2.weight"], sd[p + "conv2.bias"], padding=1), 0.2)
            return out + F.conv2d(F.interpolate(x, scale_factor=f, mode="bilinear", align_corners=False), sd[p + "skip.weight"])

        cond, skips = [], []
        feat = F.leaky_relu(F.conv2d(x, sd["conv_body_first.weight"], sd["conv_body_first.bias"]), 0.2)
        for i in range(7):
            feat = res(f"conv_body_down.{i}.", feat, 0.5)
            skips.insert(0, feat)
        feat = F.leaky_relu(F.conv2d(feat, sd["final_conv.weight"], sd["final_conv.bias"], padding=1), 0.2)
        latent = F.linear(feat.view(1, -1), sd["final_linear.weight"], sd["final_linear.bias"]).view(1, 16, 512)
        for i in range(7):
            feat = res(f"conv_body_up.{i}.", feat + skips[i], 2)
            for name in ("condition_scale", "condition_shift"):
                p = f"{name}.{i}."
                cond.append(F.conv2d(F.leaky_relu(F.conv2d(feat, sd[p + "0.weight"], sd[p + "0.bias"], padding=1), 0.2),
                                     sd[p + "2.weight"], sd[p + "2.bias"], padding=1))
        d = "stylegan_decoder."
        noise = [sd[d + f"noises.noise{i}"] for i in range(15)]
        out = styleconv(d + "style_conv1.", sd[d + "constant_input.weight"], latent[:, 0], noise[0], False)
        skip = torgb(d + "to_rgb1.", out, latent[:, 1], None)
        i = 1
        for k in range(7):
            out = styleconv(d + f"style_convs.{2 * k}.", out, latent[:, i], noise[2 * k + 1], True)
            if i < len(cond):
                h = out.shape[1] // 2
                out = torch.cat([out[:, :h], out[:, h:] * cond[i - 1] + cond[i]], 1)
            out = styleconv(d + f"style_convs.{2 * k + 1}.", out, latent[:, i + 1], noise[2 * k + 2], False)
            skip = torgb(d + f"to_rgbs.{k}.", out, latent[:, i + 2], skip)
            i += 2
        return skip

    return dict(SRVGG=SRVGGNetCompact, SPAN=SPANEval, SPANTrain=SPANTrainGraph,
                GFPGAN=GFPGANStatic, gfpgan_reference=gfpgan_reference)


# ---------------------------------------------------------------------------
# Padrão de referência do medidor (igual ao C++: reference_pattern em
# src/hyper/neural/NeuralCommon.cpp) — inteiro puro, sem arredondamento.
# ---------------------------------------------------------------------------
def reference_pattern(w: int, h: int) -> np.ndarray:
    y, x = np.mgrid[0:h, 0:w].astype(np.int64)
    out = np.zeros((h, w, 3), np.uint8)
    for c in range(3):
        # gradientes em dente de serra (bordas suaves e algumas duras) + xadrez 8×8 leve
        v = 48 + ((x * x + 2 * y * y + 500 * c) // 24) % 128 + 24 * (((x // 8) + (y // 8) + c) % 2)
        out[:, :, c] = v.astype(np.uint8)
    return out


def png_bytes(img: np.ndarray) -> bytes:
    h, w, _ = img.shape
    raw = b"".join(b"\x00" + img[r].tobytes() for r in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def calibration_images(folder: pathlib.Path, size: int = 96, count: int = 48) -> pathlib.Path:
    """Imagens sintéticas determinísticas para a calibração int8."""
    folder.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(20261025)
    y, x = np.mgrid[0:size, 0:size].astype(np.float32) / size
    names = []
    for i in range(count):
        kind = i % 6
        img = np.zeros((size, size, 3), np.float32)
        if kind == 0:   # gradientes suaves de cor
            for c in range(3):
                a, b = rng.uniform(-1, 1, 2)
                img[:, :, c] = 0.5 + 0.5 * np.sin(math.pi * (a * x + b * y + rng.uniform()))
        elif kind == 1:   # grades e listras finas
            f = rng.uniform(4, 24)
            ang = rng.uniform(0, math.pi)
            s = 0.5 + 0.5 * np.sign(np.sin(f * math.pi * (np.cos(ang) * x + np.sin(ang) * y)))
            img[:] = (s[..., None] * rng.uniform(0.3, 1, 3) + (1 - s[..., None]) * rng.uniform(0, 0.5, 3))
        elif kind == 2:   # discos e retângulos sobre fundo liso ("objetos")
            img[:] = rng.uniform(0, 1, 3)
            for _ in range(6):
                cx, cy, r = rng.uniform(0, 1), rng.uniform(0, 1), rng.uniform(0.05, 0.3)
                m = ((x - cx) ** 2 + (y - cy) ** 2) < r * r
                img[m] = rng.uniform(0, 1, 3)
        elif kind == 3:   # textura de ruído filtrado
            n = rng.normal(0, 1, (size // 4, size // 4, 3))
            n = np.kron(n, np.ones((4, 4, 1)))
            img[:] = np.clip(0.5 + 0.18 * n, 0, 1)
        elif kind == 4:   # "texto": traços horizontais/verticais finos
            img[:] = rng.uniform(0.7, 1)
            for _ in range(30):
                r0, c0 = rng.integers(0, size - 8, 2)
                if rng.uniform() < 0.5:
                    img[r0:r0 + 1 + rng.integers(0, 2), c0:c0 + rng.integers(3, 12)] = rng.uniform(0, 0.3)
                else:
                    img[r0:r0 + rng.integers(3, 12), c0:c0 + 1 + rng.integers(0, 2)] = rng.uniform(0, 0.3)
        else:   # blocos 8×8 (artefato de compressão) + ruído de sensor
            base = 0.5 + 0.4 * np.sin(math.pi * (rng.uniform(1, 3) * x + rng.uniform(1, 3) * y))[..., None] * rng.uniform(0.3, 1, 3)
            blocks = base.reshape(size // 8, 8, size // 8, 8, 3).mean((1, 3))
            img[:] = np.kron(blocks, np.ones((8, 8, 1)))
            img += rng.normal(0, 0.03, img.shape)
        if i % 4 == 3:
            img += rng.normal(0, 0.02, img.shape)
        data = np.clip(img * 255 + 0.5, 0, 255).astype(np.uint8)
        p = folder / f"calib_{i:03d}.png"
        p.write_bytes(png_bytes(data))
        names.append(str(p))
    listing = folder / "list.txt"
    listing.write_text("\n".join(names) + "\n")
    return listing


# ---------------------------------------------------------------------------
# Conversão
# ---------------------------------------------------------------------------
class Tools:
    def __init__(self, root: pathlib.Path):
        self.root = root
        venv = root / "venv" / "Scripts"
        self.pnnx = venv / "pnnx.exe"
        bins = root / "ncnn-tools" / "tools"
        self.optimize = bins / "ncnnoptimize.exe"
        self.table = bins / "quantize" / "ncnn2table.exe"
        self.int8 = bins / "quantize" / "ncnn2int8.exe"
        for t in (self.pnnx, self.optimize, self.table, self.int8):
            if not t.exists():
                sys.exit(f"ferramenta ausente: {t}")

    def run(self, args, cwd):
        print("  $", " ".join(str(a) for a in args))
        subprocess.run([str(a) for a in args], cwd=str(cwd), check=True)


def pnnx_convert(tools: Tools, module, work: pathlib.Path, name: str, shape, shape2=None):
    import torch
    work.mkdir(parents=True, exist_ok=True)
    module.eval()
    example = torch.rand(*shape)
    with torch.no_grad():
        traced = torch.jit.trace(module, example, check_trace=False)
    pt = work / f"{name}.pt"
    traced.save(str(pt))
    args = [tools.pnnx, pt.name, "inputshape=[%s]" % ",".join(map(str, shape)), "device=cpu", "fp16=0"]
    if shape2:
        args.append("inputshape2=[%s]" % ",".join(map(str, shape2)))
    tools.run(args, work)
    return work / f"{name}.ncnn.param", work / f"{name}.ncnn.bin"


def ncnn_optimize(tools: Tools, param, binf, out_param, out_bin, fp16: bool):
    tools.run([tools.optimize, param, binf, out_param, out_bin, 65536 if fp16 else 0], param.parent)


# Precisão mista: o int8 de TODAS as convoluções destrói o SPAN (a atenção
# multiplicativa amplifica o erro; com int8 na conv_2 a saída explode, média
# 5,2 em vez de 0,54) e tira o animevideov3 para 28,9 dB. Medido camada a
# camada (ncnn fixado, CPU, imagem natural 64×48 contra a mesma rede fp32):
# ficam em int8 só as convoluções que, sozinhas, custam < ~0,5 dB de PSNR
# (≥ 45 dB no SPAN, ≥ 44 dB no anime); as outras seguem fp32. Resultado
# medido da rede inteira contra a fp32: SPAN ×2 40,4 dB, ×4 39,4 dB,
# animevideov3 35,5 dB.
INT8_LAYERS = {
    "span-x2": ["conv_1", "conv_3", "conv_4", "conv_8", "conv_9"],
    "span-x4": ["conv_1", "conv_3", "conv_6", "conv_8", "conv_9", "conv_10", "conv_11", "conv_12"],
    "animevideov3": ["Conv_4", "Conv_6", "Conv_8", "Conv_16", "Conv_18", "Conv_20", "Conv_24", "Conv_26",
                     "Conv_28", "Conv_30", "Conv_32"],
}


def quantize_int8(tools: Tools, param, binf, out_param, out_bin, calib_list, shape, layers):
    table = param.with_suffix(".table")
    tools.run([tools.table, param, binf, calib_list, table, "mean=[0,0,0]",
               "norm=[0.003921569,0.003921569,0.003921569]", f"shape=[{shape},{shape},3]",
               "pixel=RGB", "thread=4", "method=kl"], param.parent)
    keep = set(layers)
    lines = [ln for ln in table.read_text().splitlines()
             if ln.split() and ln.split()[0].removesuffix("_param_0") in keep]
    if len(lines) != 2 * len(keep):
        sys.exit(f"tabela int8: esperava {2 * len(keep)} linhas para {sorted(keep)}, achei {len(lines)}")
    mixed = param.with_suffix(".mixed.table")
    mixed.write_text("\n".join(lines) + "\n")
    tools.run([tools.int8, param, binf, out_param, out_bin, mixed], param.parent)


def encode_bin16(src: pathlib.Path, dst: pathlib.Path):
    """Um .bin fp32 do ncnn (marcas de tipo 0 + floats) gravado como meia
    precisão palavra a palavra: o motor decodifica para fp32 e MISTURA dois
    destes (x4v3 ↔ wdn) antes de carregar. A marca 0x00000000 vira 0x0000."""
    words = np.frombuffer(src.read_bytes(), dtype="<u4")
    floats = words.view("<f4")
    # Toda palavra é float (as marcas de fp32 do ncnn valem 0): confere.
    if not np.all(np.isfinite(floats)):
        sys.exit(f"{src}: palavra não-float no .bin (marca de tipo inesperada)")
    dst.write_bytes(floats.astype("<f2").tobytes())


def write_reference(module, path: pathlib.Path, size: int, scale_down: int = 1):
    """Saída fp32 (torch) do padrão de referência: o medidor do aparelho compara."""
    import torch
    pat = reference_pattern(size, size)
    x = torch.from_numpy(pat.astype(np.float32) / 255.0).permute(2, 0, 1)[None]
    with torch.no_grad():
        y = module(x)
    if scale_down > 1:
        y = torch.nn.functional.avg_pool2d(y, scale_down)
    print(f"  referência {path.name}: saída em [{y.min().item():.3f}, {y.max().item():.3f}]")
    out = (y[0].permute(1, 2, 0).clamp(0, 1) * 255 + 0.5).to(torch.uint8).numpy()
    path.write_bytes(out.tobytes())
    return out.shape


def file_entry(path: pathlib.Path, role: str):
    return dict(file=path.name, role=role, bytes=path.stat().st_size, sha256=sha256_file(path))


def write_manifest(folder: pathlib.Path, info: dict, files):
    info = dict(info)
    info["files"] = [file_entry(folder / f, r) for f, r in files]
    info["totalBytes"] = sum(e["bytes"] for e in info["files"])
    (folder / "manifest.json").write_text(json.dumps(info, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"  {folder.name}: {info['totalBytes'] / 1e6:.2f} MB")


def layer_types(param: pathlib.Path):
    lines = param.read_text().split("\n")[2:]
    return sorted({ln.split()[0] for ln in lines if ln.strip()})


def convert_esrgan(tools: Tools, work: pathlib.Path):
    import torch
    mods = torch_modules()
    lic = fetch(tools.root, "Real-ESRGAN-LICENSE")
    out = ASSETS / "realesr-general-x4v3"
    out.mkdir(parents=True, exist_ok=True)
    nets = {}
    for key, tag in (("realesr-general-x4v3.pth", "x4v3"), ("realesr-general-wdn-x4v3.pth", "wdn-x4v3")):
        pth = fetch(tools.root, key)
        sd = torch.load(str(pth), map_location="cpu", weights_only=True)["params"]
        net = mods["SRVGG"]()
        net.load_state_dict(sd, strict=True)
        nets[tag] = net
        param, binf = pnnx_convert(tools, net, work / tag, f"srvgg_{tag.replace('-', '_')}", [1, 3, 64, 64], [1, 3, 96, 96])
        opt_param, opt_bin = work / tag / "opt.param", work / tag / "opt.bin"
        ncnn_optimize(tools, param, binf, opt_param, opt_bin, fp16=False)
        if tag == "x4v3":
            shutil.copyfile(opt_param, out / "srvgg-x4.param")
        elif opt_param.read_bytes() != (out / "srvgg-x4.param").read_bytes():
            sys.exit("x4v3 e wdn-x4v3 deveriam ter o MESMO grafo ncnn")
        encode_bin16(opt_bin, out / f"{tag}.bin16")
    # referência do medidor: força de ruído 0,5 (pesos misturados) e 32×32 → x4
    blend = mods["SRVGG"]()
    sa, sb = nets["x4v3"].state_dict(), nets["wdn-x4v3"].state_dict()
    blend.load_state_dict({k: 0.5 * sa[k] + 0.5 * sb[k] for k in sa})
    write_reference(blend.eval(), out / "reference-dn50-32-x4.rgb", 32)
    shutil.copyfile(lic, out / "LICENSE")
    write_manifest(out, dict(
        name="realesr-general-x4v3 + realesr-general-wdn-x4v3", version="Real-ESRGAN v0.2.5.0",
        license="BSD-3-Clause", sourceUrl=[SOURCES["realesr-general-x4v3.pth"]["url"], SOURCES["realesr-general-wdn-x4v3.pth"]["url"]],
        sourceSha256=[SOURCES["realesr-general-x4v3.pth"]["sha256"], SOURCES["realesr-general-wdn-x4v3.pth"]["sha256"]],
        architecture="SRVGGNetCompact num_feat=64 num_conv=32 upscale=4 PReLU",
        conversion="torch.jit.trace -> pnnx 20250430 fp16=0 -> ncnnoptimize(fp32) -> .bin16 (cada palavra do .bin fp32 em meia precisão)",
        runtime="o motor decodifica os dois .bin16, mistura w=denoise (x4v3) e 1-w (wdn) palavra a palavra (a interpolação de pesos do inference_realesrgan.py -dn) e carrega o .bin fp32",
        layers=layer_types(out / "srvgg-x4.param"), delivery="bundled",
        reference=dict(file="reference-dn50-32-x4.rgb", input="reference_pattern 32x32", denoise=0.5, output="128x128 RGB8")),
        [("srvgg-x4.param", "param"), ("x4v3.bin16", "weights-x4v3"), ("wdn-x4v3.bin16", "weights-wdn"),
         ("reference-dn50-32-x4.rgb", "meter-reference"), ("LICENSE", "license")])


def calibrate_span_flat_field(net, scale):
    """Corrige o ganho/deslocamento de cor do SPAN publicado.

    Os dois checkpoints (×2, ×4), rodados com o span_arch.py oficial (a
    re-parametrização confere com os ramos de treino a 1e-6), devolvem um campo
    plano v como a·v + b com b ≈ +0,03 (×2) e +0,05–0,08 (×4) por canal: a
    imagem toda sai mais clara (8–20 níveis). Contra as saídas que os PRÓPRIOS
    autores publicam no mesmo .zip (SPANX2_CH48/visualization/Set5), reduzidas
    e ampliadas de novo, a rede sem correção dá 30–32 dB e com esta correção
    39–44 dB (×2); no ×4, 29–34 dB contra 23–26 dB. A causa não foi achada (o
    "SRModelRAW" do treino não é público). A correção é medida da própria rede
    (9 campos planos, mínimos quadrados) e dobrada nos pesos da última conv:
    out' = (out − b)/a — nenhuma imagem externa entra na conversão.
    """
    import torch
    vs = torch.linspace(0.1, 0.9, 9)
    with torch.no_grad():
        outs = torch.stack([net(torch.full((1, 3, 48, 48), float(v)))[0][:, 16:-16, 16:-16].mean((1, 2)) for v in vs])
    a = torch.stack([vs, torch.ones_like(vs)], 1)
    sol = torch.linalg.lstsq(a, outs).solution
    gain, offset = sol[0], sol[1]
    g = gain.repeat_interleave(scale * scale)
    o = offset.repeat_interleave(scale * scale)
    net.up.weight.data /= g.view(-1, 1, 1, 1)
    net.up.bias.data = (net.up.bias.data - o) / g
    return gain, offset


def convert_span(tools: Tools, work: pathlib.Path):
    import torch
    mods = torch_modules()
    zpath = fetch(tools.root, "span-checkpoints.zip")
    lic = fetch(tools.root, "SPAN-LICENSE")
    calib = calibration_images(work / "calib")
    z = zipfile.ZipFile(str(zpath))
    for scale in (2, 4):
        member = f"spanx{scale}_ch48.pth"
        raw = work / member
        work.mkdir(parents=True, exist_ok=True)
        raw.write_bytes(z.read(member))
        member_sha = sha256_file(raw)
        sd = torch.load(str(raw), map_location="cpu", weights_only=True)["params"]
        net = mods["SPAN"](sd, scale).eval()
        check = mods["SPANTrain"](sd, scale)
        # No treino, o 1×1 com bias roda sobre a borda JÁ preenchida com zeros
        # (vale b1 ali); a 3×3 re-parametrizada (a avaliação oficial) vê zeros.
        # As duas só concordam longe da borda: compara o miolo (raio 21 × escala).
        # Entrada "natural" (gradientes e bordas): com ruído uniforme de faixa
        # inteira o SPAN sai do regime de treino (saídas de ±200) e qualquer
        # diferença de arredondamento explode; isso não testa a álgebra.
        yy, xx = np.mgrid[0:100, 0:100] / 100.0
        img = np.stack([0.5 + 0.4 * np.sin(6 * xx + 2 * yy), 0.5 + 0.3 * np.cos(4 * yy),
                        0.4 + 0.3 * ((xx > 0.5) ^ (yy > 0.3))], 0).astype(np.float32)
        x = torch.from_numpy(img)[None]
        with torch.no_grad():
            m = 24 * scale
            err = (net(x) - check(x))[..., m:-m, m:-m].abs().max().item()
        print(f"  SPAN x{scale}: re-parametrização vs ramos de treino (miolo), erro máx {err:.2e}")
        if err > 1e-3:
            sys.exit("re-parametrização do SPAN diverge")
        gain, offset = calibrate_span_flat_field(net, scale)
        print(f"  SPAN x{scale}: campo plano ganho {gain.tolist()} deslocamento {offset.tolist()} (corrigidos na última conv)")
        tag = f"span-x{scale}"
        param, binf = pnnx_convert(tools, net, work / tag, f"span_x{scale}", [1, 3, 64, 64], [1, 3, 96, 96])
        out = ASSETS / tag
        out.mkdir(parents=True, exist_ok=True)
        fp32p, fp32b = work / tag / "fp32.param", work / tag / "fp32.bin"
        ncnn_optimize(tools, param, binf, fp32p, fp32b, fp16=False)
        ncnn_optimize(tools, param, binf, out / f"{tag}-fp16.param", out / f"{tag}-fp16.bin", fp16=True)
        # int8 do SPAN: medido e RECUSADO (ver INT8_LAYERS): nem o misto vale
        # o custo (−2,6 dB numa imagem de teste contra o fp32, ~6% de tempo no
        # host). Fica só o fp16; a CPU usa conta fp16 onde houver (ARMv8.2).
        for stale in (out / f"{tag}-int8.param", out / f"{tag}-int8.bin"):
            stale.unlink(missing_ok=True)
        write_reference(net, out / f"reference-32-x{scale}.rgb", 32)
        shutil.copyfile(lic, out / "LICENSE")
        write_manifest(out, dict(
            name=f"SPAN x{scale} ch48", version="SPAN (CVPR-W NTIRE 2024), checkpoint spanx%d_ch48.pth" % scale,
            license="Apache-2.0 (repositório oficial; os pesos são publicados pelos autores no README, sem licença própria)",
            sourceUrl=SOURCES["span-checkpoints.zip"]["url"], sourceSha256=SOURCES["span-checkpoints.zip"]["sha256"],
            sourceMember=member, sourceMemberSha256=member_sha, stateDictKey="params",
            architecture="SPAN feature_channels=48, Conv3XC re-parametrizadas em 3x3 (álgebra de Conv3XC.update_params)",
            flatFieldCorrection=dict(gain=[round(float(v), 6) for v in gain], offset=[round(float(v), 6) for v in offset],
                                     note="out' = (out - offset)/gain dobrado na última conv; ver calibrate_span_flat_field"),
            conversion="torch.jit.trace -> pnnx 20250430 -> correção de campo plano -> ncnnoptimize fp16 (flag 65536)",
            int8="recusado: int8 em todas as conv explode (conv_2); misto (5/22 ×2, 8/22 ×4) = 40,4/39,6 dB do fp32 e só ~6% mais rápido no host",
            layers=layer_types(out / f"{tag}-fp16.param"),
            delivery="bundled", reference=dict(file=f"reference-32-x{scale}.rgb", input="reference_pattern 32x32",
                                               output=f"{32 * scale}x{32 * scale} RGB8")),
            [(f"{tag}-fp16.param", "param-fp16"), (f"{tag}-fp16.bin", "weights-fp16"),
             (f"reference-32-x{scale}.rgb", "meter-reference"), ("LICENSE", "license")])


def convert_anime(tools: Tools, work: pathlib.Path):
    """animevideov3 já embutido (engine/assets/ai): só a versão int8 da CPU Lite."""
    src = REPO / "engine" / "assets" / "ai"
    calib = calibration_images(work / "calib")
    out = ASSETS / "realesr-animevideov3"
    out.mkdir(parents=True, exist_ok=True)
    work.mkdir(parents=True, exist_ok=True)
    # Os pesos embutidos são fp16; para calibrar, primeiro em fp32.
    fp32p, fp32b = work / "anime-fp32.param", work / "anime-fp32.bin"
    ncnn_optimize(tools, src / "realesr-animevideov3-x4.param", src / "realesr-animevideov3.bin", fp32p, fp32b, fp16=False)
    # ncnnoptimize renomeia o blob? o .param do x4 usa "data"/"output".
    quantize_int8(tools, fp32p, fp32b, out / "animevideov3-x4-int8.param", out / "animevideov3-x4-int8.bin", calib, 96,
                  INT8_LAYERS["animevideov3"])
    shutil.copyfile(src / "LICENSE-Real-ESRGAN.txt", out / "LICENSE")
    write_manifest(out, dict(
        name="realesr-animevideov3 x4 int8", version="Real-ESRGAN v0.2.5.0 (ncnn oficial já embutido em engine/assets/ai)",
        license="BSD-3-Clause", sourceUrl="engine/assets/ai/realesr-animevideov3.bin (realesrgan-ncnn-vulkan-20220424)",
        sourceSha256=sha256_file(src / "realesr-animevideov3.bin"),
        conversion="ncnnoptimize fp32 -> ncnn2table kl (48 imagens sintéticas) -> ncnn2int8 só nas convoluções de int8Layers (precisão mista)",
        int8Layers=INT8_LAYERS["animevideov3"], int8PsnrVsFp32Db=35.5,
        layers=layer_types(out / "animevideov3-x4-int8.param"), delivery="bundled"),
        [("animevideov3-x4-int8.param", "param-int8"), ("animevideov3-x4-int8.bin", "weights-int8"), ("LICENSE", "license")])


def convert_rife(tools: Tools, work: pathlib.Path):
    param = fetch(tools.root, "rife-v4.6-flownet.param")
    binf = fetch(tools.root, "rife-v4.6-flownet.bin")
    lic = fetch(tools.root, "rife-ncnn-vulkan-LICENSE")
    lic2 = fetch(tools.root, "Practical-RIFE-LICENSE")
    out = ASSETS / "rife-v4.6"
    out.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(param, out / "flownet.param")
    shutil.copyfile(binf, out / "flownet.bin")
    (out / "LICENSE").write_text(
        "RIFE v4.6 (ncnn) — rife-ncnn-vulkan, tag 20221029 (MIT):\n\n" + lic.read_text(encoding="utf-8")
        + "\n\nModelo RIFE (Practical-RIFE / ECCV2022-RIFE, MIT):\n\n" + lic2.read_text(encoding="utf-8"), encoding="utf-8")
    write_manifest(out, dict(
        name="RIFE v4.6 flownet", version="rife-ncnn-vulkan 20221029 models/rife-v4.6", license="MIT",
        sourceUrl=[SOURCES["rife-v4.6-flownet.param"]["url"], SOURCES["rife-v4.6-flownet.bin"]["url"]],
        sourceSha256=[SOURCES["rife-v4.6-flownet.param"]["sha256"], SOURCES["rife-v4.6-flownet.bin"]["sha256"]],
        conversion="nenhuma: param/bin oficiais (pesos fp16). A camada rife.Warp é implementada pelo Aurea (src/hyper/neural/MotionRife.cpp)",
        blobs=dict(inputs=["in0", "in1", "in2 (timestep)"], flow="327 (4 canais: fluxo para in0 e para in1)",
                   mask="332 (sigmoid)", output="out0"),
        layers=layer_types(out / "flownet.param"), delivery="bundled"),
        [("flownet.param", "param"), ("flownet.bin", "weights-fp16"), ("LICENSE", "license")])


def convert_gfpgan(tools: Tools, work: pathlib.Path):
    import torch
    mods = torch_modules()
    pth = fetch(tools.root, "GFPGANv1.4.pth")
    lic = fetch(tools.root, "GFPGAN-LICENSE")
    sd = torch.load(str(pth), map_location="cpu", weights_only=True)["params_ema"]
    net = mods["GFPGAN"](sd).eval()
    rng = np.random.default_rng(7)
    x = torch.from_numpy(rng.uniform(-1, 1, (1, 3, 512, 512)).astype(np.float32))
    with torch.no_grad():
        a = net(x)
        b = mods["gfpgan_reference"](sd, x)
    err = (a - b).abs().max().item()
    print(f"  GFPGAN: forma estática vs conv modulada original, erro máx {err:.2e}")
    if err > 1e-3:
        sys.exit("reformulação do GFPGAN diverge")
    tag = "gfpgan-v1.4"
    param, binf = pnnx_convert(tools, net, work / tag, "gfpgan_v14", [1, 3, 512, 512])
    if "Eltwise" in layer_types(param):
        sys.exit("GFPGAN: o pnnx gerou Eltwise (sem broadcast no ncnn); ver modconv")
    out = tools.root / "out" / "ondemand" / tag
    out.mkdir(parents=True, exist_ok=True)
    ncnn_optimize(tools, param, binf, out / "gfpgan-v1.4-fp16.param", out / "gfpgan-v1.4-fp16.bin", fp16=True)
    # referência: entrada = padrão 512 em [-1,1], saída RGB8 de (y+1)/2
    pat = reference_pattern(512, 512)
    xin = torch.from_numpy(pat.astype(np.float32) / 127.5 - 1.0).permute(2, 0, 1)[None]
    with torch.no_grad():
        y = net(xin)
    ref = (((y[0].permute(1, 2, 0) + 1) * 0.5).clamp(0, 1) * 255 + 0.5).to(torch.uint8).numpy()
    (out / "reference-512.rgb").write_bytes(ref.tobytes())
    shutil.copyfile(lic, out / "LICENSE")
    info = dict(
        name="GFPGAN v1.4 (GFPGANv1Clean 512)", version="GFPGAN release v1.3.4, GFPGANv1.4.pth", license="Apache-2.0",
        licenseNotes=("O LICENSE do GFPGAN lista componentes de terceiros: basicsr (Apache-2.0), stylegan2-pytorch "
                      "(MIT), StyleGAN2 da NVIDIA (licença NVIDIA, só nos kernels CUDA da versão NÃO clean — não "
                      "usados) e DFDNet (CC BY-NC-SA 4.0, código não usado aqui). Os pesos foram treinados no FFHQ "
                      "(imagens sob CC BY-NC-SA 4.0): risco jurídico a avaliar antes de vender."),
        sourceUrl=SOURCES["GFPGANv1.4.pth"]["url"], sourceSha256=SOURCES["GFPGANv1.4.pth"]["sha256"],
        stateDictKey="params_ema",
        architecture=("GFPGANv1Clean out_size=512 channel_multiplier=2 different_w sft_half input_is_latent; "
                      "conv modulada reescrita sem pesos dinâmicos (forma não fundida do StyleGAN2); ruído = buffers fixos"),
        conversion="torch.jit.trace -> pnnx 20250430 -> ncnnoptimize fp16 (flag 65536)",
        io="entrada in0 [3,512,512] RGB em [-1,1] (rosto alinhado ao gabarito FFHQ); saída out0 [3,512,512] em [-1,1]",
        layers=layer_types(out / "gfpgan-v1.4-fp16.param"), delivery="on-demand (fora do APK)",
        referenceMaxErrorVsOriginalFormulation=err)
    write_manifest(out, info, [("gfpgan-v1.4-fp16.param", "param"), ("gfpgan-v1.4-fp16.bin", "weights-fp16"),
                               ("reference-512.rgb", "meter-reference"), ("LICENSE", "license")])
    # Os hashes que o motor confere ao abrir o modelo baixado.
    inl = REPO / "engine" / "src" / "hyper" / "neural" / "OndemandHashes.inl"
    lines = ["// Gerado por tools/hyper_models.py (convert_gfpgan) a partir do manifesto do",
             "// modelo sob demanda. {id, arquivo, SHA-256, bytes}"]
    for e in json.loads((out / "manifest.json").read_text(encoding="utf-8"))["files"]:
        if e["role"] != "license":
            lines.append(f'{{"{tag}", "{e["file"]}", "{e["sha256"]}", {e["bytes"]}ull}},')
    inl.write_text("\n".join(lines) + "\n", encoding="utf-8")
    # No repositório: só o manifesto e a licença (o modelo é sob demanda).
    repo = ASSETS / tag
    repo.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(out / "manifest.json", repo / "manifest.json")
    shutil.copyfile(lic, repo / "LICENSE")


STEPS = dict(esrgan=convert_esrgan, span=convert_span, anime=convert_anime, rife=convert_rife, gfpgan=convert_gfpgan)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tools", default=str(REPO / "build" / "hyper-tools"))
    ap.add_argument("--only", default=",".join(STEPS))
    args = ap.parse_args()
    tools = Tools(pathlib.Path(args.tools).resolve())
    for name in args.only.split(","):
        print(f"== {name}")
        STEPS[name](tools, tools.root / "work" / name)


if __name__ == "__main__":
    main()
