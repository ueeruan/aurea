"""STOP da Aurea AI — desliga na ordem certa.

Rode esta celula para encerrar. A ordem importa:

 1. avisar o discovery (`online:false`) ENQUANTO o tunel ainda responde —
    depois de derrubar o cloudflared nao ha mais por onde avisar;
 2. encerrar o cloudflared;
 3. encerrar o ComfyUI.

O app tambem valida `/system_stats` antes de dizer Online, entao ele fica
Offline sozinho mesmo se este STOP nunca rodar (queda de sessao, por exemplo).
O aviso daqui so antecipa o que o app ja descobriria — nao e a unica defesa.
"""
from __future__ import annotations

import os
import subprocess
import sys
import time
import urllib.request

WORKER = "https://aurea-ai-discovery.aureaapp.workers.dev"
SECRET_NO_COLAB = "AUREA_DISCOVERY_SECRET"


def log(msg: str) -> None:
    print(f"[aurea] {msg}", flush=True)


def ler_secret() -> str:
    valor = os.environ.get(SECRET_NO_COLAB, "")
    if not valor:
        try:
            from google.colab import userdata  # type: ignore

            valor = userdata.get(SECRET_NO_COLAB) or ""
        except Exception:  # noqa: BLE001
            valor = ""
    return valor


def marcar_offline() -> bool:
    segredo = ler_secret()
    if not segredo:
        log(f"sem {SECRET_NO_COLAB}: nao consigo avisar o discovery")
        return False
    corpo = b'{"endpoint": "", "online": false}'
    pedido = urllib.request.Request(
        WORKER + "/publicar", data=corpo, method="POST",
        headers={"content-type": "application/json",
                 "user-agent": "Aurea-Discovery/1.0 (+https://aurea.app)",
                 "authorization": f"Bearer {segredo}"},
    )
    try:
        with urllib.request.urlopen(pedido, timeout=20) as r:
            log(f"discovery avisado (HTTP {r.status})")
            return r.status in (200, 201)
    except Exception as e:  # noqa: BLE001
        log(f"nao consegui avisar o discovery: {e}")
        return False


def encerrar(padrao: str, rotulo: str) -> None:
    try:
        saida = subprocess.run(["pgrep", "-f", padrao], capture_output=True, text=True).stdout.split()
    except Exception:  # noqa: BLE001
        saida = []
    for pid in saida:
        try:
            os.kill(int(pid), 15)
        except Exception:  # noqa: BLE001
            pass
    if saida:
        time.sleep(2)
        log(f"{rotulo} encerrado ({len(saida)} processo(s))")
    else:
        log(f"{rotulo} nao estava rodando")


def main() -> None:
    log("=== Aurea AI — STOP ===")
    # 1) avisa ANTES de derrubar o tunel: depois nao ha por onde avisar.
    marcar_offline()
    # 2) e 3) derruba.
    encerrar("cloudflared", "cloudflared")
    encerrar("ComfyUI/main.py", "ComfyUI")
    log("Aurea AI desligada.")


main()
