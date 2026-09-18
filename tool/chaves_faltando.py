# -*- coding: utf-8 -*-
"""LISTA AS CHAVES USADAS NO APP QUE NAO EXISTEM NO CATALOGO.

Uma chave que passa por `AppText` mas nao esta em `languages.tsv` e o pior
dos casos: PARECE traduzida e nao esta. A tela abre em japones e aquela
frase sai em portugues.

O QUE ELE TRATA, e que a varredura simples erra:

  * LITERAL PARTIDO EM VARIAS LINHAS — `'Uma frase longa '\n  'que
    continua'` e um texto so. Lido linha a linha ele vira dois pedacos
    truncados, e a chave gerada nao existe;
  * LITERAL COM INTERPOLACAO — `'Excluir ${n} projetos?'` e uma chave
    legitima (o Dart guarda o texto com `$`), e o catalogador precisa
    receber o texto cru;
  * `\\n` DENTRO DO LITERAL — no TSV ele viaja como os dois caracteres, e
    a chave e o texto com a quebra de verdade.

USO:
    python tool/chaves_faltando.py          # lista as chaves
    python tool/chaves_faltando.py --tsv    # so as chaves, uma por linha
"""
import csv
import io
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TSV = os.path.join(RAIZ, 'tool', 'languages.tsv')

# Um literal Dart de aspas simples, SEM quebra de linha dentro.
LITERAL = re.compile(r"'((?:[^'\\\n]|\\.)*)'")
CHAMADA = re.compile(r'(?:AppText|translate|translateFor)\s*\(')
TEM_LETRA = re.compile(r'[A-Za-zÀ-ÿ]')


def ler_catalogo():
    linhas = [
        l
        for l in io.open(TSV, encoding='utf-8').read().splitlines()
        if l.strip()
    ]
    rows = list(csv.reader(linhas, delimiter='\t'))
    codigos = rows[0][1:]
    chaves = set()
    for row in rows[1:]:
        if len(row) != len(codigos) + 1:
            continue
        chaves.add(row[0].replace('\\n', '\n'))
    return codigos, chaves


def juntar_literais(fonte, pos):
    """Junta os literais ADJACENTES de uma chamada, a partir do `(` em [pos].

    `AppText('Uma frase longa '
  'que continua aqui')` e UM texto: o
    Dart concatena literais vizinhos na compilacao. Lido linha a linha ele
    vira dois pedacos truncados, e a chave gerada nao existe no catalogo —
    o relatorio encheria de falso positivo e esconderia o defeito de
    verdade.

    A REGRA DE PARADA: o primeiro argumento que NAO seja um literal encerra
    a leitura. Sem ela, `AppText('x', style: TextStyle(...))` continuaria
    varrendo o `style` e recolheria literais que nao sao texto de tela.
    """
    if pos >= len(fonte) or fonte[pos] != '(':
        return None, 0
    i = pos + 1
    pedacos = []
    while i < len(fonte):
        c = fonte[i]
        if c.isspace() or c == ',':
            # Virgula so encerra se ja lemos algo e o que vem nao e literal.
            if c == ',' and pedacos:
                j = i + 1
                while j < len(fonte) and fonte[j].isspace():
                    j += 1
                if j >= len(fonte) or fonte[j] != "'":
                    break
            i += 1
            continue
        if c == ')':
            break
        if c == "'":
            m = LITERAL.match(fonte, i)
            if m is None:
                # Aspas que nao fecham na linha: o literal e partido, e a
                # continuacao esta na linha seguinte.
                j = fonte.find("'", i + 1)
                if j < 0:
                    return None, 0
                pedacos.append(fonte[i + 1 : j])
                i = j + 1
                continue
            pedacos.append(m.group(1))
            i = m.end()
            continue
        # Qualquer outra coisa (identificador, numero, chamada) encerra.
        break
    if not pedacos:
        return None, 0
    return ''.join(pedacos), len(pedacos)


def varrer():
    codigos, chaves = ler_catalogo()
    faltando = {}
    for dp, _, fs in os.walk(os.path.join(RAIZ, 'lib')):
        for f in sorted(fs):
            if not f.endswith('.dart'):
                continue
            caminho = os.path.join(dp, f)
            rel = os.path.relpath(caminho, RAIZ).replace('\\', '/')
            if '/l10n/' in rel:
                continue
            fonte = io.open(caminho, encoding='utf-8').read()
            for m in CHAMADA.finditer(fonte):
                texto, quantos = juntar_literais(fonte, m.end() - 1)
                if texto is None:
                    continue
                if not TEM_LETRA.search(texto):
                    continue
                if texto in chaves:
                    continue
                # A chave do catalogo e o texto com a quebra DE VERDADE.
                real = texto.replace('\\n', '\n')
                if real in chaves:
                    continue
                linha = fonte[: m.start()].count('\n') + 1
                faltando.setdefault(real, []).append(f'{rel}:{linha}')
    return codigos, chaves, faltando


def main():
    codigos, chaves, faltando = varrer()
    if '--tsv' in sys.argv:
        for k in sorted(faltando):
            print(k.replace('\n', '\\n'))
        return
    print(f'catalogo: {len(chaves)} chaves, {len(codigos)} idiomas')
    print(f'FALTANDO: {len(faltando)} chaves usadas que nao existem\n')
    for k in sorted(faltando, key=lambda s: (len(s), s)):
        onde = faltando[k]
        print(f'  [{len(k):3d}] {k!r}')
        print(f'         {onde[0]}')


if __name__ == '__main__':
    main()
