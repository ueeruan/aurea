"""Conta literais de texto no Kotlin para dimensionar a localizacao.

Nao e um extrator: e um MEDIDOR. Ele diz quantas strings distintas existem e
quais sao, para decidir o tamanho do catalogo. Comentarios sao removidos antes
da contagem, senao o portugues dos comentarios entra na conta.
"""
import collections
import os
import re
import sys

STRING = re.compile(r'"((?:[^"\\]|\\.){2,})"')
LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
WORDY = re.compile(r"[A-Za-zÀ-ÿ]{2}")

# Coisas que claramente nao sao texto de interface.
SKIP = (
    "aurea/", "com/", "android.", "java.", "http", "kotlin", "Compose",
    "androidx.", "sRGB", "rgba", "video/", "image/", "%",
)


def collect(root):
    all_strings = collections.Counter()
    per_file = collections.Counter()
    for base, _, files in os.walk(root):
        for name in files:
            if not name.endswith(".kt"):
                continue
            path = os.path.join(base, name)
            src = open(path, encoding="utf-8").read()
            src = BLOCK_COMMENT.sub("", src)
            src = LINE_COMMENT.sub("", src)
            for match in STRING.finditer(src):
                s = match.group(1)
                if len(s) < 2 or not WORDY.search(s):
                    continue
                if any(k in s for k in SKIP):
                    continue
                all_strings[s] += 1
                per_file[path] += 1
    return all_strings, per_file


if __name__ == "__main__":
    root = sys.argv[1] if len(sys.argv) > 1 else "."
    strings, per_file = collect(root)
    print("literais distintos:", len(strings))
    print("ocorrencias        :", sum(strings.values()))
    print("arquivos com texto :", len(per_file))
    print()
    print("--- arquivos com mais texto ---")
    for path, count in per_file.most_common(15):
        print(f"{count:5d}  {path}")
    print()
    print("--- literais mais repetidos ---")
    for s, c in strings.most_common(20):
        print(f"{c:4d}  {s[:60]!r}")
