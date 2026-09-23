"""Converte os literais de interface em recursos e reescreve o Kotlin.

Duas etapas separadas de propósito:

  `scan`   — varre e escreve `tools/i18n_ptbr.tsv` (nome do recurso + texto).
             Nada é modificado. É o que se revisa.
  `apply`  — lê o TSV, injeta os recursos no `strings.xml` padrão e troca os
             literais no Kotlin por `stringResource(R.string.<nome>)`.

O nome do recurso sai do TEXTO (slug estável), não da posição: mover a linha não
muda a chave, e a mesma frase em dois lugares vira o MESMO recurso — é o que faz
a tradução ser escrita uma vez só.

O que ele RECUSA a converter, de propósito:
  - literal com interpolação (`"total ${n}"`): viraria um recurso com
    placeholder e precisa de decisão humana sobre o formato;
  - literal fora de contexto de interface (chave de lista, id, MIME).
"""
import hashlib
import os
import re
import sys
import unicodedata

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i18n_extract import classify, looks_like_text, STRING  # noqa: E402

ROOT = "android/app/src/main/java/com/aurea/aurea"
RES = "android/app/src/main/res/values/strings.xml"
TSV = "tools/i18n_ptbr.tsv"

# Não ajudam a identificar o recurso sozinhas.
STOP = {"o", "a", "os", "as", "de", "da", "do", "das", "dos", "em", "no", "na",
        "um", "uma", "para", "por", "com", "e", "que", "se", "ao", "à"}

# Contexto → prefixo do nome. O prefixo diz de que TELA é o texto, que é a
# única coisa que continua legível quando o valor está em árabe.
PREFIX = [
    ("/home/", "home"),
    ("/editor/panels/", "panel"),
    ("/editor/timeline/", "timeline"),
    ("/editor/", "editor"),
    ("/effects/", "effect"),
    ("/captions/", "caption"),
    ("/presets/", "preset"),
    ("/ui/ds/", "ds"),
    ("/ui/", "ui"),
    ("/state/", "state"),
    ("/engine/", "engine"),
]

# Mesmo comprimento do original: mantém os offsets válidos para a reescrita.
COMMENT_MASK = re.compile(r"//[^\n]*|/\*.*?\*/", re.S)

# Literais que PARECEM texto mas são IDENTIDADE. Traduzir qualquer um destes
# quebra o app em silêncio: são comparados, concatenados em chave de cache, ou
# passados a uma API do sistema que espera o nome exato.
EXCLUDE_EXACT = {
    "AndroidKeyStore", "AES/GCM/NoPadding", "SHA-1", "SHA-256",
    "AureaEngine", "AureaStore", "Aurea",
    "English", "Español", "Português", "Русский", "हिन्दी", "Bahasa Indonesia", "العربية",
    "Full", "Auto", "CPU", "GPU",
    "image/svg+xml", "video/mp4", "application/json",
}
EXCLUDE_PREFIX = ("OMX.", "c2.", "VP9Profile", "avc1.", "hvc1.", "\\p{", "[axis", "$")

# Extensão de arquivo e separador de chave de cache (".meta.json", ".blur.").
EXCLUDE_SHAPE = re.compile(r"^\.[a-z0-9.]*$|^[a-z0-9_]+(\.[a-z0-9_]+)+$")


def masked(src: str) -> str:
    """Comentários viram espaço — o texto continua alinhado com o original."""
    return COMMENT_MASK.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), src)


def slug(text: str) -> str:
    plain = unicodedata.normalize("NFKD", text)
    plain = "".join(c for c in plain if not unicodedata.combining(c))
    plain = re.sub(r"[^a-zA-Z0-9 ]+", " ", plain)
    words = [w.lower() for w in plain.split() if w.lower() not in STOP]
    name = "_".join(words[:6]) or "texto"
    return re.sub(r"_{2,}", "_", name).strip("_")[:60]


def prefix_for(path: str) -> str:
    p = path.replace("\\", "/")
    for frag, pre in PREFIX:
        if frag in p:
            return pre
    return "app"


ANNOTATION = re.compile(r"@Composable\b")


def composable_spans(src: str):
    """Intervalos [início, fim] de cada função `@Composable` do arquivo.

    Por que isto existe: `stringResource` só pode ser chamado de dentro de uma
    função `@Composable`. Converter um literal que mora numa TABELA DE DADOS
    (lista de opções de um painel, `enum`), num `object`, ou numa função comum
    não dá erro de compilação claro — dá uma cascata deles, um por linha.

    A tabela de dados PRECISA de tratamento próprio (guardar ID de recurso em
    vez de texto) e isso é decisão humana, arquivo por arquivo: converter aqui
    seria adivinhar.
    """
    spans = []
    for m in ANNOTATION.finditer(src):
        # Acha a assinatura e o corpo: primeiro `{` no nível zero depois dela.
        i = src.find("(", m.end())
        if i < 0:
            continue
        depth = 0
        j = i
        while j < len(src):
            if src[j] == "(":
                depth += 1
            elif src[j] == ")":
                depth -= 1
                if depth == 0:
                    break
            j += 1
        # Corpo: `{` ... `}` casado, ou `= expr` até o fim da linha.
        k = src.find("{", j)
        eol = src.find("\n", j)
        if k < 0 or (0 <= eol < k):
            continue
        depth = 0
        e = k
        while e < len(src):
            if src[e] == "{":
                depth += 1
            elif src[e] == "}":
                depth -= 1
                if depth == 0:
                    break
            e += 1
        spans.append((k, e))
    return spans


def candidates():
    """(path, start, end, texto) de cada literal que É texto de interface."""
    for base, _, files in os.walk(ROOT):
        for name in sorted(files):
            if not name.endswith(".kt"):
                continue
            path = os.path.join(base, name)
            raw = open(path, encoding="utf-8").read()
            src = masked(raw)
            spans = composable_spans(src)
            if not spans:
                continue
            for m in STRING.finditer(src):
                # Só dentro de um corpo `@Composable`.
                if not any(a <= m.start() < b for a, b in spans):
                    continue
                value = m.group(1)
                if not looks_like_text(value):
                    continue
                if "$" in value:
                    continue                      # interpolação: decisão humana
                if value in EXCLUDE_EXACT or value.startswith(EXCLUDE_PREFIX):
                    continue
                if EXCLUDE_SHAPE.match(value):
                    continue
                if "{" in value or "}" in value or "%.1f" in value:
                    continue
                if classify(src[max(0, m.start() - 80):m.start()]) == "tech":
                    continue
                yield path, m.start(), m.end(), value


def assign_keys(rows):
    """Dá um nome de recurso a cada texto, sem colisão.

    Dois textos diferentes podem gerar o mesmo slug. O desempate tem de ser
    DETERMINÍSTICO e independente da ordem de varredura, senão `scan` e `apply`
    discordam — e foi exatamente o que aconteceu na primeira versão: um texto
    perdia a colisão no TSV e o `apply` procurava por ele em vão.

    Regra: o texto lexicograficamente menor fica com o nome puro; os outros
    levam um sufixo derivado do próprio texto.
    """
    base_of = {}
    for _path, _s, _e, value in rows:
        base_of.setdefault(f"{prefix_for(_path)}_{slug(value)}", set()).add(value)

    suffix = {}
    for base, texts in base_of.items():
        if len(texts) == 1:
            continue
        for text in sorted(texts)[1:]:
            suffix[text] = hashlib.md5(text.encode("utf-8")).hexdigest()[:4]

    out = []
    for path, start, end, value in rows:
        base = f"{prefix_for(path)}_{slug(value)}"
        key = f"{base}_{suffix[value]}" if value in suffix else base
        out.append((path, start, end, value, key))
    return out


def scan():
    rows = assign_keys(list(candidates()))
    found = {}
    for path, _s, _e, value, key in rows:
        entry = found.setdefault(key, {"pt": value, "uses": 0, "files": set()})
        entry["uses"] += 1
        entry["files"].add(path)
    with open(TSV, "w", encoding="utf-8", newline="\n") as f:
        f.write("key\tpt\tuses\tfiles\n")
        for key in sorted(found):
            e = found[key]
            f.write(f"{key}\t{e['pt']}\t{e['uses']}\t{len(e['files'])}\n")
    print(f"{len(found)} recursos em {TSV}")


def escaped(pt: str) -> str:
    out = pt.replace("&", "&amp;").replace("<", "&lt;")
    # O apóstrofo solto é erro fatal do aapt2 ("Invalid unicode escape").
    return out.replace("'", "\\'")


def apply():
    rows = []
    with open(TSV, encoding="utf-8") as f:
        next(f)
        for line in f:
            key, pt, _u, _f = line.rstrip("\n").split("\t")
            rows.append((key, pt))
    by_text = {pt: key for key, pt in rows}

    # 1) Recursos no catálogo padrão (pt-BR).
    xml = open(RES, encoding="utf-8").read()
    add = [f'    <string name="{k}">{escaped(pt)}</string>'
           for k, pt in rows if f'name="{k}"' not in xml]
    if add:
        xml = xml.replace(
            "</resources>",
            "\n    <!-- Texto da interface — gerado de tools/i18n_ptbr.tsv -->\n"
            + "\n".join(add) + "\n\n</resources>", 1)
        open(RES, "w", encoding="utf-8", newline="\n").write(xml)

    # 2) Reescreve o Kotlin por OFFSET (não por replace global: a mesma frase
    #    pode aparecer num contexto que não é de interface).
    per_file = {}
    for path, start, end, _value, key in assign_keys(list(candidates())):
        per_file.setdefault(path, []).append((start, end, key))

    for path, edits in per_file.items():
        raw = open(path, encoding="utf-8").read()
        out = raw
        for start, end, key in sorted(edits, reverse=True):
            out = out[:start] + f"stringResource(R.string.{key})" + out[end:]
        if "import androidx.compose.ui.res.stringResource" not in out:
            out = out.replace(
                "import androidx.compose.ui.Modifier",
                "import androidx.compose.ui.Modifier\nimport androidx.compose.ui.res.stringResource", 1)
        if "import com.aurea.aurea.R\n" not in out:
            out = out.replace(
                "import androidx.compose.ui.res.stringResource\n",
                "import androidx.compose.ui.res.stringResource\nimport com.aurea.aurea.R\n", 1)
        open(path, "w", encoding="utf-8", newline="\n").write(out)
    print(f"{len(add)} recursos novos; {len(per_file)} arquivos reescritos")


if __name__ == "__main__":
    {"scan": scan, "apply": apply}[sys.argv[1]]()
