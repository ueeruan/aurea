"""Os pesos do H3 em `/content/models`.

Regra do pedido (§20): ausente = trazer SO aquele arquivo. Presente = validar e
nao trazer de novo.

Como a validacao funciona: existe um manifesto (`models/manifest.json`) com
tamanho e hash de cada peso, escrito por `scripts/preparar_modelos.py` depois do
primeiro download bom. Com manifesto, o servidor confere tamanho e o hash dos
primeiros MB; arquivo truncado e recusado antes de entrar na fila. Sem
manifesto, o servidor exige apenas que o arquivo exista e nao esteja vazio, e
avisa no log — porque inventar um tamanho esperado seria pior: um piso errado
recusaria um modelo bom.
"""
from __future__ import annotations

import hashlib
import json
import logging
import shutil
from dataclasses import dataclass
from pathlib import Path
from typing import Any

log = logging.getLogger("aurea_ai.models")

ARQUIVO_MANIFESTO = "manifest.json"
PEDACO_HASH = 16 * 1024 * 1024

DESCRICAO = {
    "minimax_h3_fl2va_pruned_int8_convrot.safetensors": "DiT do H3 (int8)",
    "qwen3vl_32b_minimax_h3_nvfp4_awq.safetensors": "Codificador de texto Qwen3-VL 32B",
    "minimax_h3_video_vae_int8_convrot.safetensors": "VAE de video",
    "minimax_h3_audio_vae_fp32.safetensors": "VAE de audio",
    "minimax_h3_turbo_lora.safetensors": "LoRA Turbo",
}


@dataclass
class Situacao:
    nome: str
    caminho: Path
    existe: bool
    tamanho: int
    # `problema` impede gerar. `aviso` nao impede: e informacao.
    problema: str = ""
    aviso: str = ""

    @property
    def ok(self) -> bool:
        return self.existe and not self.problema

    def linha(self) -> str:
        estado = "ok" if self.ok else (self.problema or "faltando")
        if self.aviso:
            estado = f"{estado} ({self.aviso})"
        return f"{self.nome:<52} {_mb(self.tamanho):>10}  {estado}"


def _mb(n: int) -> str:
    return f"{n / (1024 ** 2):.1f} MB"


def hash_curto(caminho: Path, pedaco: int = PEDACO_HASH) -> str:
    """Hash dos primeiros MB. Pega download interrompido ou arquivo trocado sem
    ler 12 GB, e e rapido o bastante para caber no boot do Colab."""
    h = hashlib.sha256()
    lido = 0
    with caminho.open("rb") as f:
        while lido < pedaco:
            bloco = f.read(min(1024 * 1024, pedaco - lido))
            if not bloco:
                break
            h.update(bloco)
            lido += len(bloco)
    return h.hexdigest()[:16]


def ler_manifesto(models_dir: Path) -> dict[str, Any]:
    caminho = models_dir / ARQUIVO_MANIFESTO
    if not caminho.is_file():
        return {}
    try:
        d = json.loads(caminho.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        log.warning("manifesto ilegivel (%s); seguindo sem ele", e)
        return {}
    return d.get("arquivos", {}) if isinstance(d, dict) else {}


def conferir(models_dir: Path, exigidos: list[str], manifesto: dict | None = None) -> list[Situacao]:
    """Le o disco. Nao baixa, nao copia."""
    if manifesto is None:
        manifesto = ler_manifesto(models_dir)

    achados: list[Situacao] = []
    for nome in dict.fromkeys(exigidos):
        caminho = models_dir / nome
        if not caminho.is_file():
            achados.append(Situacao(nome, caminho, False, 0, "faltando"))
            continue

        tamanho = caminho.stat().st_size
        if tamanho == 0:
            achados.append(Situacao(nome, caminho, True, 0, "arquivo vazio"))
            continue

        registrado = manifesto.get(nome)
        if not registrado:
            # Sem manifesto nao ha o que comparar. Nao bloqueia — mas fica dito,
            # porque so o manifesto pega arquivo truncado.
            achados.append(Situacao(nome, caminho, True, tamanho,
                                    aviso="sem manifesto"))
            continue

        esperado = registrado.get("tamanho")
        if esperado and tamanho != esperado:
            achados.append(Situacao(
                nome, caminho, True, tamanho,
                f"tamanho difere ({_mb(tamanho)} vs {_mb(esperado)})"))
            continue

        esperado_hash = registrado.get("hash")
        if esperado_hash:
            real = hash_curto(caminho)
            if real != esperado_hash:
                achados.append(Situacao(nome, caminho, True, tamanho, "hash difere"))
                continue

        achados.append(Situacao(nome, caminho, True, tamanho, ""))
    return achados


def faltando(situacoes: list[Situacao]) -> list[str]:
    """O que impede gerar. `sem manifesto` NAO entra: um piso de tamanho
    inventado recusaria um modelo bom."""
    return [s.nome for s in situacoes if not s.ok]


def registrar_manifesto(models_dir: Path, exigidos: list[str]) -> Path:
    """Escreve tamanho + hash dos pesos que estao no disco. Rode depois do
    primeiro download bom."""
    manifesto = {"arquivos": {}}
    for situacao in conferir(models_dir, exigidos, manifesto={}):
        if not situacao.existe or situacao.tamanho == 0:
            log.warning("nao registro %s: %s", situacao.nome,
                    situacao.problema or situacao.aviso or "ausente")
            continue
        manifesto["arquivos"][situacao.nome] = {
            "tamanho": situacao.tamanho,
            "hash": hash_curto(situacao.caminho),
            "descricao": DESCRICAO.get(situacao.nome, ""),
        }
    caminho = models_dir / ARQUIVO_MANIFESTO
    caminho.write_text(json.dumps(manifesto, indent=2, ensure_ascii=False), encoding="utf-8")
    return caminho


def trazer_do_drive(models_dir: Path, origem_drive: Path, exigidos: list[str]) -> list[str]:
    """Copia do Drive SO o que falta ou esta corrompido.

    Nao sobrescreve arquivo bom: o Drive pode ter uma copia mais antiga e
    recopiar 12 GB a cada boot do Colab nao e aceitavel.
    """
    models_dir.mkdir(parents=True, exist_ok=True)
    trouxeram: list[str] = []
    for situacao in conferir(models_dir, exigidos):
        if situacao.ok:
            continue
        fonte = origem_drive / situacao.nome
        if not fonte.is_file():
            log.warning("nao esta no Drive: %s", situacao.nome)
            continue
        if situacao.existe:
            situacao.caminho.unlink(missing_ok=True)
        log.info("copiando %s do Drive (%s)", situacao.nome, situacao.problema or "ausente")
        shutil.copy2(fonte, situacao.caminho)
        trouxeram.append(situacao.nome)
    return trouxeram


def resumo(models_dir: Path, exigidos: list[str]) -> str:
    return "\n".join(s.linha() for s in conferir(models_dir, exigidos))


def exigidos_pela_biblioteca(biblioteca) -> list[str]:
    nomes: list[str] = []
    for wf in biblioteca.todos():
        for n in wf.modelos_exigidos():
            if n not in nomes:
                nomes.append(n)
    return nomes
