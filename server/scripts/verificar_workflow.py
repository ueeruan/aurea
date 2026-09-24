"""Confere os workflows contra o ComfyUI instalado, antes de gerar.

Por que isso existe: os arquivos `workflows/h3_*.json` trazem nomes de classe
(`MiniMaxH3Sampler`, `MiniMaxH3AudioDecode`, ...). Se o pack de nodes do H3
instalado usar outros nomes, o servidor so descobriria isso no meio de um job,
depois de a A100 ficar ocupada. Aqui a resposta sai em dois segundos, com a
lista exata do que falta.

Uso:
    python -m scripts.verificar_workflow http://127.0.0.1:8188
"""
from __future__ import annotations

import json
import sys
import urllib.error
import urllib.request
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
if str(RAIZ) not in sys.path:
    sys.path.insert(0, str(RAIZ))

from aurea_ai.workflows import Biblioteca, WorkflowInvalido  # noqa: E402

# Classes do proprio ComfyUI. Ausentes aqui significam instalacao incompleta —
# bem diferente de "o pack do H3 usa outro nome".
NUCLEO = {"UNETLoader", "CLIPLoader", "VAELoader", "LoraLoaderModelOnly",
          "CLIPTextEncode", "VAEDecode", "LoadImage"}


def object_info(url: str, segundos: int = 60) -> dict:
    alvo = url.rstrip("/") + "/object_info"
    try:
        with urllib.request.urlopen(alvo, timeout=segundos) as r:
            return json.loads(r.read().decode("utf-8"))
    except urllib.error.URLError as e:
        raise SystemExit(f"nao consegui falar com o ComfyUI em {alvo}: {e}")


def conferir(biblioteca: Biblioteca, disponiveis: set[str]) -> list[tuple[str, str]]:
    """(workflow, classe) de cada classe que o workflow usa e o ComfyUI nao tem."""
    faltam: list[tuple[str, str]] = []
    for wf in biblioteca.todos():
        for nome in sorted(wf.class_names()):
            if nome not in disponiveis and (wf.nome, nome) not in faltam:
                faltam.append((wf.nome, nome))
    return faltam


def principal(argv: list[str]) -> int:
    url = argv[1] if len(argv) > 1 else "http://127.0.0.1:8188"

    biblioteca = Biblioteca(RAIZ / "workflows")
    try:
        biblioteca.carregar()
    except WorkflowInvalido as e:
        print(f"workflow invalido: {e}")
        return 2

    info = object_info(url)
    disponiveis = set(info)

    print(f"ComfyUI: {url}")
    print(f"nodes disponiveis: {len(disponiveis)}")
    print()

    saida = 0
    problemas: list[tuple[str, str]] = []
    for wf in biblioteca.todos():
        print(f"[{wf.nome}]")
        print(f"  nodes no grafo: {len(wf.grafo)}")
        for nome in sorted(wf.class_names()):
            tem = nome in disponiveis
            marca = "ok " if tem else "FALTA"
            print(f"  {marca} {nome}")
            if not tem:
                problemas.append((wf.nome, nome))
        for modelo in wf.modelos_exigidos():
            print(f"  peso: {modelo}")
        print()

    if problemas:
        saida = 1
        print("=" * 70)
        print("RESOLVA ANTES DE GERAR")
        print("=" * 70)
        for wf_nome, classe in problemas:
            dica = ("falta no ComfyUI — instale o pack ou corrija a instalacao"
                    if classe in NUCLEO else
                    "nome de classe diferente do seu pack: edite "
                    f"workflows/{wf_nome}.json e troque '{classe}' pelo nome certo")
            print(f"  {classe}  ({wf_nome})")
            print(f"      {dica}")
        print()
        print("O servidor nao precisa ser alterado: so o arquivo do workflow.")

    return saida


if __name__ == "__main__":
    raise SystemExit(principal(sys.argv))
