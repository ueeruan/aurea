"""Grava e confere as auditorias dos fixtures de paridade (docs/parity/fixtures).

    python tools/parity_fixtures.py            # confere (código 1 se divergir)
    python tools/parity_fixtures.py --record   # regrava as auditorias
    python tools/parity_fixtures.py --list     # só mostra o que há

POR QUE ISTO EXISTE

Os fixtures são a ENTRADA dos testes de paridade: um `.gltf` sintético cujo
conteúdo não é o que foi revisado transforma o teste em teatro. Cada um tem um
`<nome>.audit.json` ao lado com o sha256 do arquivo, e a captura no simulador
recusa o fixture quando o hash não bate.

O QUE DEU ERRADO (e é o motivo da canonicalização abaixo)

O projeto é desenvolvido no Windows e compilado no macOS. O `.gltf` foi gravado
com CRLF; a auditoria registrou o hash DESSE arquivo. No commit, o git
normalizou para LF (o repo é `* text=auto eol=lf`), o CI recebeu LF, o hash não
bateu e as duas cenas de face morreram com

    RuntimeError: Synthetic front-face fixture does not match its audited bytes

O hash não estava errado — estava medindo uma coisa que o git não guarda. Aqui a
identidade de um fixture de TEXTO é sempre a forma LF, que é o que o repositório
armazena e o que qualquer checkout entrega.

BINÁRIO NÃO SE NORMALIZA. `.aurea` é um contêiner binário com seções e checksums
e `.hdr` é Radiance binário: converter `\r\n` dentro deles corromperia o arquivo.
A extensão decide — ver `TEXTO`.
"""
import hashlib
import json
import sys
from pathlib import Path

RAIZ = Path(__file__).resolve().parent.parent
FIXTURES = RAIZ / "docs" / "parity" / "fixtures"

# Extensões cujo conteúdo é TEXTO e cuja identidade é a forma LF. Tudo o que não
# está aqui é tratado como binário e o hash sai dos bytes crus.
TEXTO = {".gltf", ".json", ".obj", ".mtl", ".txt", ".xml", ".svg", ".md"}

# Campos que a captura no simulador exige em cada auditoria, por tipo de fixture.
CAMPOS_BASE = ("sha256", "bytes", "source", "description")


def canonico(path: Path) -> bytes:
    """Os bytes que o repositório guarda: LF para texto, cru para binário."""
    raw = path.read_bytes()
    if path.suffix.lower() in TEXTO:
        return raw.replace(b"\r\n", b"\n")
    return raw


def digerir(path: Path) -> dict:
    dados = canonico(path)
    return {"sha256": hashlib.sha256(dados).hexdigest(), "bytes": len(dados)}


def alvos():
    """Todo fixture (o que não é `.audit.json` nem o manifest)."""
    if not FIXTURES.is_dir():
        return []
    return sorted(p for p in FIXTURES.iterdir()
                  if p.is_file() and not p.name.endswith(".audit.json"))


def carregar(path: Path):
    p = path.with_suffix(".audit.json")
    if not p.is_file():
        return None, p
    return json.loads(p.read_text(encoding="utf-8")), p


def gravar(path: Path, registro: dict) -> Path:
    destino = path.with_suffix(".audit.json")
    textos = sorted(set(registro) - {"sha256", "bytes", "externalResources"})
    ordenado = {}
    for campo in CAMPOS_BASE:
        if campo in registro:
            ordenado[campo] = registro[campo]
    for campo in textos:
        ordenado[campo] = registro[campo]
    destino.write_text(json.dumps(ordenado, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return destino


def conferir() -> int:
    problemas = []
    itens = alvos()
    if not itens:
        print("nenhum fixture em %s" % FIXTURES)
        return 1
    for path in itens:
        registro, destino = carregar(path)
        dados = digerir(path)
        if registro is None:
            problemas.append("%s: sem auditoria (%s)" % (path.name, destino.name))
            continue
        if registro.get("sha256") != dados["sha256"]:
            problemas.append("%s: sha256 da auditoria %s, do arquivo %s"
                             % (path.name, str(registro.get("sha256"))[:16], dados["sha256"][:16]))
        if registro.get("bytes") not in (None, dados["bytes"]):
            problemas.append("%s: tamanho da auditoria %s, do arquivo %d"
                             % (path.name, registro.get("bytes"), dados["bytes"]))
        for campo in CAMPOS_BASE:
            if campo not in registro:
                problemas.append("%s: auditoria sem o campo %s" % (path.name, campo))
        # Um fixture que aponta para mídia externa não é autocontido: no
        # simulador ele abriria sem textura e o teste passaria por engano.
        if registro.get("externalResources", 0) not in (0, None):
            problemas.append("%s: declara %s recurso(s) externo(s)"
                             % (path.name, registro["externalResources"]))
    print("fixtures: %d" % len(itens))
    if problemas:
        print("PROBLEMAS (%d):" % len(problemas))
        for p in problemas:
            print("  " + p)
        return 1
    print("0 problema(s) - toda auditoria bate com o arquivo (identidade em LF para texto)")
    return 0


def regravar() -> int:
    itens = alvos()
    for path in itens:
        registro, destino = carregar(path)
        registro = dict(registro or {})
        registro.setdefault("source", path.name)
        registro.setdefault("description", "fixture de paridade")
        registro.update(digerir(path))
        gravar(path, registro)
        print("gravado %-42s %s" % (destino.name, registro["sha256"][:16]))
    return 0


def listar() -> int:
    for path in alvos():
        registro, _ = carregar(path)
        marca = "ok " if registro and registro.get("sha256") == digerir(path)["sha256"] else "!! "
        print("%s%-42s %s" % (marca, path.name, (registro or {}).get("sha256", "(sem auditoria)")[:16]))
    return 0


if __name__ == "__main__":
    if "--record" in sys.argv:
        sys.exit(regravar())
    if "--list" in sys.argv:
        sys.exit(listar())
    sys.exit(conferir())
