"""Traz os pesos do H3 do Drive e registra o manifesto.

Rodar no Colab, depois de montar o Drive:

    python -m scripts.preparar_modelos --drive /content/drive/MyDrive/AureaAI/models
    python -m scripts.preparar_modelos --drive ... --registrar

Sem `--registrar` ele so relata. Com `--registrar`, escreve
`/content/models/manifest.json` com tamanho e hash de cada peso — e a partir dai
o servidor passa a recusar arquivo truncado antes de enfileirar um job.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[1]
if str(RAIZ) not in sys.path:
    sys.path.insert(0, str(RAIZ))

from aurea_ai.models import (conferir, exigidos_pela_biblioteca, faltando,  # noqa: E402
                             registrar_manifesto, trazer_do_drive)
from aurea_ai.workflows import Biblioteca, WorkflowInvalido  # noqa: E402


def principal(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Pesos do H3 em /content/models")
    p.add_argument("--modelos", default="/content/models", type=Path)
    p.add_argument("--drive", type=Path, help="pasta no Drive de onde copiar o que falta")
    p.add_argument("--registrar", action="store_true",
                   help="grava o manifesto com tamanho e hash do que esta no disco")
    args = p.parse_args(argv[1:])

    biblioteca = Biblioteca(RAIZ / "workflows")
    try:
        biblioteca.carregar()
    except WorkflowInvalido as e:
        print(f"workflow invalido: {e}")
        return 2

    exigidos = exigidos_pela_biblioteca(biblioteca)
    args.modelos.mkdir(parents=True, exist_ok=True)

    if args.drive:
        if not args.drive.is_dir():
            print(f"Drive nao montado ou pasta inexistente: {args.drive}")
            return 2
        vieram = trazer_do_drive(args.modelos, args.drive, exigidos)
        print(f"copiados do Drive: {len(vieram)}")
        for nome in vieram:
            print(f"  {nome}")
        print()

    print(f"pasta: {args.modelos}")
    print("=" * 78)
    for situacao in conferir(args.modelos, exigidos):
        print("  " + situacao.linha())
    print("=" * 78)

    faltam = faltando(conferir(args.modelos, exigidos))
    if faltam:
        print(f"FALTAM {len(faltam)}: o servidor vai responder, mas nao aceita gerar.")
        for nome in faltam:
            print(f"  {nome}")
        return 1

    if args.registrar:
        caminho = registrar_manifesto(args.modelos, exigidos)
        print(f"manifesto escrito: {caminho}")
    else:
        print("tudo no lugar. Rode de novo com --registrar para fixar tamanho e hash.")

    return 0


if __name__ == "__main__":
    raise SystemExit(principal(sys.argv))
