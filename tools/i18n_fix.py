"""Desfaz a conversão SÓ nos pontos que o compilador recusou.

O `i18n_build apply` converte todo literal de interface que está dentro de um
corpo `@Composable`. Alguns estão dentro de uma LAMBDA daquele corpo que não é
composable (`remember { }`, `onClick` de um `LaunchedEffect`, `when` dentro de um
`derivedStateOf`) — e ali `stringResource` não pode ser chamado.

Consertar site a site à mão seriam dezenas de edições; este script faz o inverso:
roda o compilador, lê `arquivo:linha:coluna`, e devolve o literal NAQUELA LINHA.
Repete até o build passar. Os outros usos do mesmo texto continuam convertidos.
"""
import re
import subprocess
import sys

TSV = "tools/i18n_ptbr.tsv"
ERROR = re.compile(r"^e: file:///(.*?):(\d+):(\d+) (.*)$")
CONVERTED = re.compile(r"stringResource\(R\.string\.([a-z0-9_]+)\)")


def load():
    pt = {}
    with open(TSV, encoding="utf-8") as f:
        next(f)
        for line in f:
            key, text, _u, _fl = line.rstrip("\n").split("\t")
            pt[key] = text
    return pt


def build():
    """Chama o gradle pelo MESMO caminho do terminal (bash + ./gradlew).

    `cmd /c gradlew.bat` devolve rc=1 com 113 bytes de saída: o .bat não acha o
    Java pelo PATH do Python. O bash é o ambiente em que o build funciona.
    """
    out = subprocess.run(
        ["bash", "-c",
         "JAVA_HOME=C:/Users/SnyX/AppData/Local/Java/jdk-21.0.12.1+1 "
         "./gradlew :app:assembleDebug -PaureaAbi=x86_64 2>&1"],
        cwd="android", capture_output=True, text=True, encoding="utf-8", errors="replace",
    )
    return out.stdout + out.stderr


def main():
    pt = load()
    for round_no in range(1, 25):
        log = build()
        errors = []
        for line in log.splitlines():
            m = ERROR.match(line.strip())
            if m:
                errors.append((m.group(1), int(m.group(2))))
        if "BUILD SUCCESSFUL" in log:
            print(f"verde depois de {round_no - 1} rodadas de correcao")
            return 0
        if not errors:
            print("build falhou sem erro de stringResource; ver o log")
            print("\n".join(l for l in log.splitlines() if l.startswith("e:"))[:2000])
            return 1

        fixed = 0
        for path, lineno in errors:
            full = path.replace("%20", " ")
            try:
                lines = open(full, encoding="utf-8").read().split("\n")
            except OSError:
                continue
            idx = lineno - 1
            if idx >= len(lines):
                continue
            match = CONVERTED.search(lines[idx])
            if not match or match.group(1) not in pt:
                continue
            value = pt[match.group(1)].replace("\\'", "'")
            lines[idx] = (lines[idx][:match.start()] + '"' + value + '"'
                          + lines[idx][match.end():])
            open(full, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
            fixed += 1
        print(f"rodada {round_no}: {len(errors)} erros, {fixed} revertidos")
        if fixed == 0:
            print("nada mais a reverter; ver o log")
            return 1
    return 1


if __name__ == "__main__":
    sys.exit(main())
