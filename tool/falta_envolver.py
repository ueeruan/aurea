# -*- coding: utf-8 -*-
"""SEPARA O QUE FALTA TRADUZIR DO QUE SO FALTA ENVOLVER.

Os literais acentuados que nao passam por `AppText`/`translate` sao duas
coisas MUITO diferentes, e trata-las igual faz o trabalho parecer dez
vezes maior do que e:

  * JA NO CATALOGO — a traducao existe, e o lugar simplesmente nao a pede.
    Conserto: envolver a chamada. Zero traducao nova;
  * FALTAM — a frase nunca foi cadastrada. Conserto: traduzir.

O relatorio sai em UTF-8 no arquivo, e nao no console: a saida padrao do
Windows come os acentos, e um relatorio de traducao que perde acento nao
serve para nada.
"""
import csv
import io
import os
import re

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LITERAL = re.compile(r"'((?:[^'\\\n]|\\.)*)'")
ACENTO = re.compile(r'[ãõçáéíóúâêôàÃÕÇÁÉÍÓÚÂÊÔ]')
TRAD = re.compile(r'(?:AppText|translate|translateFor)\s*\(')


def catalogo():
    linhas = [
        l
        for l in io.open(
            os.path.join(RAIZ, 'tool', 'languages.tsv'), encoding='utf-8'
        ).read().splitlines()
        if l.strip()
    ]
    rows = list(csv.reader(linhas, delimiter='\t'))
    # A LINHA TEM 10 COLUNAS: o portugues mais os NOVE idiomas. O
    # cabecalho tambem tem 10 — `codigos` e o cabecalho sem o portugues.
    # Filtrar por 11 nao casaria com linha nenhuma, e o relatorio diria
    # que NADA esta no catalogo — o exagero mais perigoso possivel num
    # relatorio de trabalho faltante.
    return {r[0].replace('\\n', '\n') for r in rows[1:] if len(r) == 10}


def varrer():
    """(local, string) de todo literal acentuado fora da traducao."""
    achados = []
    for dp, _, fs in os.walk(os.path.join(RAIZ, 'lib')):
        for f in sorted(fs):
            if not f.endswith('.dart'):
                continue
            caminho = os.path.join(dp, f)
            rel = os.path.relpath(caminho, RAIZ).replace(os.sep, '/')
            if '/l10n/' in rel:
                continue
            for n, linha in enumerate(
                io.open(caminho, encoding='utf-8').read().splitlines(), 1
            ):
                if linha.strip().startswith('//'):
                    continue
                for m in LITERAL.finditer(linha):
                    v = m.group(1)
                    if not ACENTO.search(v) or len(v) < 3:
                        continue
                    if TRAD.search(linha[: m.start()]):
                        continue
                    achados.append((f'{rel}:{n}', v))
    return achados


def main():
    cat = catalogo()
    ja, falta = {}, {}
    for onde, v in varrer():
        (ja if v in cat else falta).setdefault(v, onde)

    saida = [
        '=== JA NO CATALOGO (%d) — so falta ENVOLVER, sem traducao nova ==='
        % len(ja),
    ]
    for v in sorted(ja, key=lambda s: (len(s), s)):
        saida.append(f'  {v}\t{ja[v]}')
    saida.append('')
    saida.append('=== FALTAM (%d) — precisam de traducao nova ===' % len(falta))
    for v in sorted(falta, key=lambda s: (len(s), s)):
        saida.append(f'  {v}\t{falta[v]}')
    io.open(
        os.path.join(RAIZ, 'tool', '_relatorio.txt'), 'w', encoding='utf-8'
    ).write('\n'.join(saida))
    print(f'ja no catalogo: {len(ja)}   faltam: {len(falta)}')


if __name__ == '__main__':
    main()
