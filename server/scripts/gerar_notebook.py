"""Gera colab/aurea_h3_server.ipynb.

O notebook e escrito por este script de proposito: editar .ipynb a mao erra
virgula e a celula inteira deixa de abrir. Aqui o JSON e montado pelo modulo
`json`, sem escape manual.
"""
from __future__ import annotations

import json
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
DESTINO = RAIZ / "colab" / "aurea_h3_server.ipynb"


def md(texto: str) -> dict:
    return {"cell_type": "markdown", "metadata": {}, "source": texto.splitlines(keepends=True)}


def codigo(texto: str) -> dict:
    return {"cell_type": "code", "execution_count": None, "metadata": {},
            "outputs": [], "source": texto.splitlines(keepends=True)}


CELULAS = [
    md("""# Aurea AI — MiniMax H3 no Colab (A100)

Este notebook sobe tres coisas, sempre na mesma ordem:

1. **ComfyUI** com os nodes do MiniMax H3 (motor de geracao)
2. **Aurea AI API** (FastAPI) na frente dele — e a unica coisa que o app fala
3. **Tunel + discovery** — para o app achar o servidor sozinho

O aplicativo **nao** guarda IP, porta nem endereco de tunel. Ele le um documento
de discovery que este notebook reescreve a cada 20 segundos. Quando o Colab cai,
o documento envelhece e o app mostra "Offline" sozinho.

Antes de rodar, cadastre em **🔑 (barra lateral) → Secrets**:

| segredo | para que |
|---|---|
| `AUREA_SERVER_TOKENS` | token(s) de cliente, um por tester, separados por virgula |
| `AUREA_ADMIN_TOKEN` | token administrativo — nunca sai daqui |
| `AUREA_DISCOVERY_REPO` | `owner/repo` onde publicar o discovery |
| `AUREA_DISCOVERY_TOKEN` | token do GitHub com escrita nesse repo |
| `AUREA_BRANCH` | branch do repo do app (padrao: `main`) |

Testador com token proprio e o que separa o historico de cada um — e o que
permite revogar um aparelho perdido sem trocar o token de todos.
"""),

    codigo("""#@title 1. GPU e segredos
import os, subprocess, sys

print(subprocess.run(["nvidia-smi", "--query-gpu=name,memory.total",
                      "--format=csv,noheader"],
                     capture_output=True, text=True).stdout.strip() or "sem GPU")

from google.colab import userdata

def segredo(nome, obrigatorio=True, padrao=""):
    try:
        return userdata.get(nome) or padrao
    except Exception:
        if obrigatorio:
            raise SystemExit(
                f"Falta o segredo {nome}. Abra a aba de chaves (barra esquerda) "
                f"e cadastre antes de continuar.")
        return padrao

os.environ["AUREA_SERVER_TOKENS"] = segredo("AUREA_SERVER_TOKENS")
os.environ["AUREA_ADMIN_TOKEN"]   = segredo("AUREA_ADMIN_TOKEN")
os.environ["AUREA_DISCOVERY_REPO"]  = segredo("AUREA_DISCOVERY_REPO")
os.environ["AUREA_DISCOVERY_TOKEN"] = segredo("AUREA_DISCOVERY_TOKEN")
os.environ["AUREA_DISCOVERY_BRANCH"] = segredo("AUREA_BRANCH", obrigatorio=False, padrao="main")

quantos = len([t for t in os.environ["AUREA_SERVER_TOKENS"].split(",") if t.strip()])
print(f"segredos carregados | {quantos} token(s) de cliente")
"""),

    codigo("""#@title 2. Montar o Drive (opcional, mas evita baixar 40 GB toda vez)
from google.colab import drive
import os

drive.mount("/content/drive")
PASTA = "/content/drive/MyDrive/AureaAI/models"
os.environ["AUREA_DRIVE_MODELS"] = PASTA

if os.path.isdir(PASTA):
    arquivos = sorted(os.listdir(PASTA))
    print(f"{len(arquivos)} arquivo(s) em {PASTA}")
    for a in arquivos:
        print("  ", a)
else:
    print(f"{PASTA} nao existe. Crie a pasta e ponha os .safetensors do H3 nela,")
    print("ou siga sem ela: o script vai reclamar dos pesos que faltarem.")
    os.environ.pop("AUREA_DRIVE_MODELS", None)
"""),

    codigo("""#@title 3. Trazer o repositorio do Aurea
import os, subprocess

REPO = "https://github.com/ueeruan/aurea.git"   # ajuste se o seu fork for outro
BRANCH = os.environ.get("AUREA_DISCOVERY_BRANCH", "main")
DESTINO = "/content/Aureabeta"

if not os.path.isdir(os.path.join(DESTINO, ".git")):
    subprocess.run(["git", "clone", "--depth", "1", "--branch", BRANCH, REPO, DESTINO],
                   check=True)
else:
    subprocess.run(["git", "-C", DESTINO, "pull", "--ff-only"], check=False)

os.environ["AUREA_RAIZ"] = DESTINO
print("repo em", DESTINO)
print(subprocess.run(["git", "-C", DESTINO, "log", "-1", "--oneline"],
                     capture_output=True, text=True).stdout.strip())
"""),

    codigo("""#@title 4. Subir tudo
# Idempotente: rodar de novo nao rebaixa nem reinstala o que ja esta no lugar.
import os, subprocess

raiz = os.environ["AUREA_RAIZ"]
script = os.path.join(raiz, "server", "scripts", "start_aurea_h3.sh")
r = subprocess.run(["bash", script], cwd=os.path.join(raiz, "server"),
                   env=os.environ.copy())
print("codigo de saida:", r.returncode)
if r.returncode != 0:
    print("veja /content/aurea-logs/aurea-ai.log")
"""),

    codigo("""#@title 5. Acompanhar (rode quando quiser)
import os, subprocess, time, json

RAIZ = os.environ.get("AUREA_RAIZ", "/content/Aureabeta")
BRANCH = os.environ.get("AUREA_DISCOVERY_BRANCH", "main")
REPO = os.environ.get("AUREA_DISCOVERY_REPO", "")

print("--- ComfyUI (ultimas linhas) ---")
print(subprocess.run(["tail", "-n", "5", "/content/aurea-logs/comfyui.log"],
                     capture_output=True, text=True).stdout)

print("--- Aurea AI (ultimas linhas) ---")
print(subprocess.run(["tail", "-n", "12", "/content/aurea-logs/aurea-ai.log"],
                     capture_output=True, text=True).stdout)

print("--- o que o app vai ler ---")
if REPO:
    alvo = (f"https://raw.githubusercontent.com/{REPO}/{BRANCH}/"
            f"discovery/aurea-h3.json")
    saida = subprocess.run(["curl", "-fsS", alvo], capture_output=True, text=True)
    if saida.returncode == 0:
        d = json.loads(saida.stdout)
        idade = int(time.time()) - int(d.get("updatedAt", 0))
        print(json.dumps(d, indent=2))
        print(f"batida ha {idade}s" + ("  <-- OFFLINE se passar de 90s" if idade > 90 else ""))
    else:
        print("ainda nao publicado")
else:
    print("AUREA_DISCOVERY_REPO nao definido: o app nao encontra o servidor sozinho.")
"""),

    md("""## Limites reais deste caminho

- **O Colab desconecta.** Sessao gratuita cai sozinha depois de horas; com A100
  pago tambem ha teto. Quando cai, o documento de discovery envelhece e o app
  mostra offline. Basta rodar de novo — o endereco novo se publica sozinho.
- **Um job por vez.** A A100 roda um H3 por vez (`MAX_GPU_JOBS = 1`). Os outros
  ficam na fila, com a posicao visivel no app.
- **O endereco do Quick Tunnel muda a cada reinicio.** Isso e esperado: e
  exatamente por isso que o app nao guarda endereco.
- **Os nomes de classe dos nodes do H3** no `workflows/h3_*.json` precisam
  existir no pack instalado. A celula 4 roda `verificar_workflow` e diz o que
  falta. Se o seu pack usar outros nomes, edite o JSON — o servidor nao muda.
- **Seguranca.** O Quick Tunnel expoe a API na internet. Por isso ha token de
  cliente obrigatorio, limite de taxa, teto de upload e teto de jobs por tester.
  O token administrativo nunca sai daqui.
"""),
]


def principal() -> int:
    nb = {
        "cells": CELULAS,
        "metadata": {
            "colab": {"provenance": [], "toc_visible": True},
            "kernelspec": {"display_name": "Python 3", "name": "python3"},
            "language_info": {"name": "python"},
            "accelerator": "GPU",
        },
        "nbformat": 4,
        "nbformat_minor": 0,
    }
    DESTINO.parent.mkdir(parents=True, exist_ok=True)
    DESTINO.write_text(json.dumps(nb, indent=1, ensure_ascii=False), encoding="utf-8")

    # O arquivo tem que voltar a ser exatamente este dicionario.
    volta = json.loads(DESTINO.read_text(encoding="utf-8"))
    assert volta == nb, "o notebook nao voltou igual"
    print(f"{DESTINO}  ({len(CELULAS)} celulas, {DESTINO.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(principal())
