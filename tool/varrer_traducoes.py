# -*- coding: utf-8 -*-
"""VARRE O PROJETO ATRAS DE TEXTO VISIVEL QUE NAO PASSA PELA TRADUCAO.

O QUE ELE PROCURA, e por que cada criterio existe:

  * LITERAL EM POSICAO DE UI — um `'...'` que chega a tela. O jeito de
    saber e a POSICAO: dentro de `AppText(...)`, `translate(...)` ou
    `translateFor(...)` ele esta traduzido; em `Text('...')` cru, nao;
    * CHAVE SEM TRADUCAO — um literal que passa por `AppText` mas cujo
    texto nao existe em `languages.tsv`. E o pior dos casos, porque
    PARECE traduzido e nao esta: a pessoa ve portugues no meio do japones;
    * TRADUCAO IGUAL AO ORIGINAL — a linha do TSV que repete o portugues
    numa lingua que nao e o portugues. As vezes e certo ("Aurea", nomes
    proprios), e por isso ele LISTA em vez de acusar;
    * TEXTO MONTADO — interpolacao dentro de um literal traduzido. Uma
    frase com `$` no meio e uma frase cuja traducao tem de carregar o
    marcador; quando o TSV nao tem o marcador, a traducao perde o numero.

USO:
    python tool/varrer_traducoes.py            # relatorio completo
    python tool/varrer_traducoes.py --curto    # so os totais
"""
import csv
import io
import os
import re
import sys
import unicodedata
from collections import defaultdict

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONTES = ['lib']
TSV = os.path.join(RAIZ, 'tool', 'languages.tsv')

# Um literal Dart com aspas simples. Nao trata `'''` nem raw: eles nao
# aparecem em texto de interface neste projeto, e a varredura nao precisa
# ser um parser de Dart — precisa ser teimosa e rapida.
LITERAL = re.compile(r"'((?:[^'\\\n]|\\.)*)'")
ACENTO = re.compile(r'[ãõçáéíóúâêôàÃÕÇÁÉÍÓÚÂÊÔ]')
# As posicoes em que um literal CHEGA A TELA.
TRADUZIDO = re.compile(r'(?:AppText|translate|translateFor)\s*\(')


def carregar_catalogo():
    linhas = io.open(TSV, encoding='utf-8').read().splitlines()
    linhas = [l for l in linhas if l.strip()]
    rows = list(csv.reader(linhas, delimiter='\t'))
    codigos = rows[0][1:]
    catalogo = {}
    for row in rows[1:]:
        if len(row) != len(codigos) + 1:
            continue
        catalogo[row[0].replace('\\n', '\n')] = dict(zip(codigos, row[1:]))
    return codigos, catalogo


def sem_acento(s):
    return ''.join(
        c
        for c in unicodedata.normalize('NFD', s)
        if unicodedata.category(c) != 'Mn'
    )


def varrer():
    codigos, catalogo = carregar_catalogo()
    crus = defaultdict(set)      # literal cru -> arquivos
    faltando = defaultdict(set)  # chave usada -> arquivos
    montados = defaultdict(set)  # literal com interpolacao -> arquivos
    for base in FONTES:
        for dp, _, fs in os.walk(os.path.join(RAIZ, base)):
            for f in fs:
                if not f.endswith('.dart'):
                    continue
                caminho = os.path.join(dp, f)
                rel = os.path.relpath(caminho, RAIZ).replace('\\', '/')
                if '/l10n/' in rel:
                    continue
                texto = io.open(caminho, encoding='utf-8').read()
                for n, linha in enumerate(texto.splitlines(), 1):
                    crua = linha.strip()
                    if crua.startswith('//'):
                        continue
                    for m in LITERAL.finditer(linha):
                        valor = m.group(1)
                        # Um literal sem letra nenhuma nao e texto de UI.
                        if not re.search(r'[A-Za-zÀ-ÿ؀-ۿ一-鿿가-힯ऀ-ॿ]', valor):
                            continue
                        antes = linha[: m.start()]
                        # O mesmo literal pode estar traduzido: olha para a
                        # chamada mais proxima a esquerda na mesma linha.
                        traduzido = bool(TRADUZIDO.search(antes))
                        if '$' in valor and re.search(r'[A-Za-zÀ-ÿ]', valor):
                            montados[valor].add(f'{rel}:{n}')
                        if traduzido:
                            if valor not in catalogo and sem_acento(valor) not in catalogo:
                                faltando[valor].add(f'{rel}:{n}')
                            continue
                        if not ACENTO.search(valor):
                            continue
                        if len(valor) < 3:
                            continue
                        crus[valor].add(f'{rel}:{n}')
    return codigos, catalogo, crus, faltando, montados


def main():
    codigos, catalogo, crus, faltando, montados = varrer()
    curto = '--curto' in sys.argv

    print(f'CATALOGO: {len(catalogo)} chaves, {len(codigos)} idiomas '
          f'({", ".join(codigos)})')

    # --- o que a varredura achou de pior: chave usada que nao existe ---
    print(f'\n=== CHAVE USADA SEM TRADUCAO: {len(faltando)} ===')
    if not curto:
        for k in sorted(faltando):
            print(f'  {k!r}  ({len(faltando[k])} lugares, ex.: '
                  f'{sorted(faltando[k])[0]})')

    print(f'\n=== LITERAL CRU COM ACENTO (fora de AppText): {len(crus)} ===')
    if not curto:
        for k in sorted(crus, key=lambda s: -len(crus[s])):
            print(f'  {k!r}  ({len(crus[k])} lugares, ex.: '
                  f'{sorted(crus[k])[0]})')

    # --- traducoes que repetem o portugues ---
    iguais = defaultdict(list)
    for pt, m in catalogo.items():
        for c, v in m.items():
            if c == 'pt':
                continue
            if v == pt and len(pt) > 3 and not re.match(
                r'^[A-Z0-9 .·/+-]+$', pt
            ):
                iguais[c].append(pt)
    print('\n=== TRADUCAO IGUAL AO PORTUGUES (revisar, nem tudo e erro) ===')
    for c in codigos:
        if c == 'pt':
            continue
        print(f'  {c}: {len(iguais[c])}')

    # --- texto montado ---
    comMarcador = {
        k: v for k, v in montados.items() if k in catalogo
    }
    print(f'\n=== TEXTO MONTADO COM TRADUCAO: {len(comMarcador)} ===')
    perdidos = []
    for k, v in comMarcador.items():
        for c in codigos:
            if c == 'pt':
                continue
            t = catalogo[k].get(c, '')
            if '$' in k and '$' not in t:
                perdidos.append((k, c, t))
    print(f'  traducao que perdeu o marcador: {len(perdidos)}')
    if not curto:
        for k, c, t in perdidos[:40]:
            print(f'    [{c}] {k!r} -> {t!r}')


if __name__ == '__main__':
    main()
