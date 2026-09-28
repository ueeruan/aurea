"""Auditoria: literais em PORTUGUÊS que ainda aparecem na interface.

Reusa o léxico e o filtro de contexto do `i18n_count.py` (Kotlin) e aplica o
mesmo ao Swift do app iOS; fica só o que tem cara de português (acento ou
palavra comum do pt-BR). Mensagem de log/erro de desenvolvedor já sai no filtro.

Uso: `python tools/i18n_audit_pt.py [--list]` — sai 1 se sobrar algum.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i18n_count import lex, is_visible  # noqa: E402

ROOTS = ["android/app/src/main/java", "engine/platform/ios/app"]
# Ferramentas de desenvolvedor: nunca aparecem para quem usa o app.
SKIP_FILES = ("StressBattery.kt", "DeviceReport.kt", "ExitDiagnostics.kt", "PerformanceTest.swift",
              "ParityExportProbe.swift", "ParityGestureProbe.swift", "AureaStrings.swift")
PT = re.compile(r"[ãõçáéíóúâêôàÃÕÇÁÉÍÓÚÂÊÔ]|\b(de|do|da|dos|das|para|com|sem|uma?|não|nao|em|na|ao|que|mais|menos|"
                r"erro|falhou|camadas?|efeitos?|quadros?|projetos?|salvar|abrir|ligado|desligado|nenhum|nenhuma|"
                r"toque|arraste|imagem|texto|cor|tamanho|escala|girar|opacidade|fundo|borda|sombra|brilho|"
                r"repetir|recortar|esticar|duas|entrada|saída|saida|linha|todas?|todos|ou|e)\b", re.I)
SWIFT_DEV = re.compile(r"(print|NSLog|os_log|fatalError|precondition|assert|assertionFailure|Logger\.\w+|log\.\w+)\s*\($")


def scan(path):
    src = open(path, encoding="utf-8").read()
    swift = path.endswith(".swift")
    for start, end, text, _tpl in lex(src):
        before = src[max(0, start - 120):start]
        if not is_visible(text, before, src[end:end + 12]):
            continue
        if swift and SWIFT_DEV.search(before.rstrip()[-60:] + "") :
            continue
        if not PT.search(text.replace("{}", " ")):
            continue
        yield src.count("\n", 0, start) + 1, text


def main():
    listing = "--list" in sys.argv
    total, files = 0, {}
    for root in ROOTS:
        for base, _, names in os.walk(root):
            for name in sorted(names):
                if not name.endswith((".kt", ".swift")) or name in SKIP_FILES:
                    continue
                path = os.path.join(base, name).replace(os.sep, "/")
                rows = list(scan(path))
                if not rows:
                    continue
                files[path] = len(rows)
                total += len(rows)
                if listing:
                    for line, text in rows:
                        print(f"{path}:{line}\t{text}")
    if not listing:
        for p, n in sorted(files.items(), key=lambda kv: -kv[1]):
            print(f"{n:5d}  {p}")
    print(f"literais em portugues: {total} em {len(files)} arquivos")
    return 1 if total else 0


if __name__ == "__main__":
    sys.exit(main())
