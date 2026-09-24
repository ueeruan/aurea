"""Publica o endereco do tunel no discovery — o pedaco que falta no START.

O START do Colab hoje faz: Drive -> restore -> modelos -> ComfyUI -> Cloudflare
-> valida a API externa. Falta so o ultimo passo: contar ao mundo qual e o
endereco de agora.

Use assim, no fim da celula START:

    from publicar_discovery import publicar
    publicar("https://xxxxx.trycloudflare.com", gpu="NVIDIA A100-SXM4-80GB")

O que a funcao faz, em ordem — e para na primeira que falhar:

 1. espera o DNS do tunel resolver;
 2. `GET {endpoint}/system_stats` ate dar HTTP 200 com JSON valido
    (o Cloudflare leva alguns segundos para propagar);
 3. so entao publica `{"online": true, "endpoint": ...}` no Worker.

Credencial necessaria, e SO esta:

    AUREA_DISCOVERY_SECRET

Ela e o segredo que o Worker guarda (`wrangler secret put`). No Colab, defina
antes de chamar:

    import os
    os.environ["AUREA_DISCOVERY_SECRET"] = "..."   # ou pelos Secrets do Colab

Esta variavel vive SO no Colab e no Worker. Ela NUNCA entra no APK nem no IPA —
o app so LE o documento publico em `/server`.
"""
from __future__ import annotations

import json
import os
import socket
import time
import urllib.error
import urllib.request
from urllib.parse import urlparse

# A Cloudflare do Worker responde 403 (erro 1010) para o User-Agent
# padrao do urllib. Um UA proprio resolve — e e o mesmo que o app manda.
UA = "Aurea-Discovery/1.0 (+https://aurea.app)"

WORKER = "https://aurea-ai-discovery.aureaapp.workers.dev"
CAMINHO_PUBLICAR = "/publicar"

# O Cloudflare leva alguns segundos entre criar o tunel e o nome resolver.
TENTATIVAS_DNS = 30
TENTATIVAS_SAUDE = 40
ESPERA_S = 3


def _log(msg: str) -> None:
    print(f"[discovery] {msg}", flush=True)


def esperar_dns(endpoint: str) -> bool:
    host = urlparse(endpoint).hostname or ""
    for i in range(TENTATIVAS_DNS):
        try:
            socket.getaddrinfo(host, 443)
            _log(f"DNS ok: {host}")
            return True
        except socket.gaierror:
            time.sleep(ESPERA_S)
    _log(f"DNS nao resolveu: {host}")
    return False


def esperar_system_stats(endpoint: str) -> bool:
    """HTTP 200 E JSON com 'system' — 200 de portal de wifi nao serve."""
    url = endpoint.rstrip("/") + "/system_stats"
    for i in range(TENTATIVAS_SAUDE):
        try:
            pedido = urllib.request.Request(url, headers={"User-Agent": UA})
            with urllib.request.urlopen(pedido, timeout=20) as r:
                if r.status == 200:
                    corpo = json.loads(r.read().decode("utf-8", "replace"))
                    if isinstance(corpo, dict) and "system" in corpo:
                        _log(f"/system_stats ok ({i + 1}a tentativa)")
                        return True
                    _log("respondeu 200, mas nao e o ComfyUI")
        except (urllib.error.URLError, TimeoutError, OSError) as e:
            _log(f"/system_stats ainda nao: {e}")
        except json.JSONDecodeError:
            _log("respondeu 200, mas o corpo nao e JSON")
        time.sleep(ESPERA_S)
    _log("/system_stats nao respondeu a tempo")
    return False


def publicar(endpoint: str, *, gpu: str = "", modelo: str = "MiniMax-H3",
             capacidades: list[str] | None = None, segredo: str | None = None,
             exigir_saude: bool = True) -> bool:
    """Conta ao discovery qual e o endereco de agora. `True` quando publicou."""
    endpoint = (endpoint or "").strip().rstrip("/")
    if not endpoint.startswith("https://"):
        _log(f"endpoint invalido (precisa de https://): {endpoint!r}")
        return False

    segredo = segredo or os.environ.get("AUREA_DISCOVERY_SECRET", "")
    if not segredo:
        _log("falta AUREA_DISCOVERY_SECRET (variavel de ambiente do Colab)")
        return False

    if exigir_saude:
        if not esperar_dns(endpoint):
            return False
        if not esperar_system_stats(endpoint):
            return False

    corpo = json.dumps({
        "endpoint": endpoint,
        "online": True,
        "gpu": gpu,
        "model": modelo,
        "capabilities": capacidades or ["text_to_video", "image_to_video"],
    }).encode("utf-8")

    pedido = urllib.request.Request(
        WORKER + CAMINHO_PUBLICAR, data=corpo, method="POST",
        headers={"content-type": "application/json", "user-agent": UA,
                 "authorization": f"Bearer {segredo}"},
    )
    try:
        with urllib.request.urlopen(pedido, timeout=30) as r:
            _log(f"publicado: {r.status} {r.read().decode('utf-8', 'replace')[:200]}")
            return r.status in (200, 201)
    except urllib.error.HTTPError as e:
        _log(f"o Worker recusou ({e.code}): {e.read().decode('utf-8', 'replace')[:200]}")
        return False
    except Exception as e:  # noqa: BLE001
        _log(f"nao consegui publicar: {e}")
        return False


def marcar_offline(segredo: str | None = None) -> bool:
    """Avisa que o servidor saiu do ar. O app mostra "Offline" na hora."""
    segredo = segredo or os.environ.get("AUREA_DISCOVERY_SECRET", "")
    if not segredo:
        return False
    corpo = json.dumps({"endpoint": "", "online": False}).encode("utf-8")
    pedido = urllib.request.Request(
        WORKER + CAMINHO_PUBLICAR, data=corpo, method="POST",
        headers={"content-type": "application/json", "user-agent": UA,
                 "authorization": f"Bearer {segredo}"},
    )
    try:
        with urllib.request.urlopen(pedido, timeout=20) as r:
            _log(f"offline publicado: {r.status}")
            return True
    except Exception as e:  # noqa: BLE001
        _log(f"nao consegui marcar offline: {e}")
        return False
