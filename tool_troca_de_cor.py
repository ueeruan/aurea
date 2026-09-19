"""Troca os hexes de marca pelos papeis de AureaColors, em todo o lib/.

Escreve um relatorio do que trocou e em que arquivo. So mexe nos hexes da
tabela abaixo: qualquer outra cor do projeto (cor de efeito, cor escolhida
pelo usuario, cor de tipo de midia) fica intacta.
"""
import io
import os
import re
import sys

RAIZ = os.path.join('lib')
SUB = os.path.join('lib', 'src', 'core', 'theme')
IMPORT = "import 'package:aurea/src/core/theme/aurea_colors.dart';"

TABELA = [
    # marca antiga
    ('0xFFB8FF3D', 'AureaColors.accent'),          # lima  -> acao
    ('0xFF7C62FF', 'AureaColors.selectionText'),   # violeta -> selecao
    ('0xFF1ED6B1', 'AureaColors.accent'),          # teal  -> realce
    ('0xFF0FA88C', 'AureaColors.keyframe'),
    ('0xFF2A3A16', 'AureaColors.accentDim'),
    ('0xFF183F3C', 'AureaColors.keyframeDim'),
    ('0xFF43B7C6', 'AureaColors.brandLight'),
    ('0xFF81D8E0', 'AureaColors.brandSoft'),
    ('0xFF0B0E12', 'AureaColors.onAccent'),
    ('0xFF7BC300', 'AureaColors.lightAccent'),
    ('0xFFE3F5C2', 'AureaColors.lightAccentDim'),
    ('0xFF6A4FF0', 'AureaColors.lightSelection'),
    ('0xFFCDEFE7', 'AureaColors.lightKeyframeDim'),
    # neutros escuros
    ('0xFF12151A', 'AureaColors.bg'),
    ('0xFF171C23', 'AureaColors.surface'),
    ('0xFF1E242E', 'AureaColors.surfaceHigh'),
    ('0xFF262C36', 'AureaColors.chip'),
    ('0xFFE9EDF2', 'AureaColors.text'),
    ('0xFF8B94A3', 'AureaColors.muted'),
    ('0xFF2A313C', 'AureaColors.border'),
    ('0xFF08080C', 'AureaColors.stage'),
    ('0xFF0E0E13', 'AureaColors.chrome'),
    ('0xFF15151D', 'AureaColors.chromeHigh'),
    ('0xFF1A1A24', 'AureaColors.chip'),
    ('0xFF1A1A28', 'AureaColors.field'),
    # claros
    ('0xFFF4F5F7', 'AureaColors.lightBg'),
    ('0xFFEDEFF3', 'AureaColors.lightSurfaceHigh'),
    ('0xFFE4E7EC', 'AureaColors.lightChip'),
    ('0xFF14171C', 'AureaColors.lightText'),
    ('0xFF6B7280', 'AureaColors.lightMuted'),
    ('0xFFD5D9E0', 'AureaColors.lightBorder'),
]

# `Color(0x...)` e `const Color(0x...)`: o invólucro inteiro sai, para nao
# sobrar `Color(AureaColors.x)`.
PADRAO = re.compile(r'(?:const\s+)?Color\((0x[0-9A-Fa-f]{8})\)')

relatorio = []
for pasta, _, arquivos in os.walk(RAIZ):
    for nome in arquivos:
        if not nome.endswith('.dart'):
            continue
        caminho = os.path.join(pasta, nome)
        if os.path.abspath(os.path.dirname(caminho)) == os.path.abspath(SUB):
            continue  # os proprios arquivos do tema sao a tabela
        with io.open(caminho, encoding='utf-8', newline='') as f:
            texto = f.read()
        trocas = []
        for hexa, papel in TABELA:
            def _sub(m, papel=papel, hexa=hexa, trocas=trocas):
                if m.group(1).upper() == hexa.upper():
                    trocas.append(hexa)
                    return papel
                return m.group(0)
            texto = PADRAO.sub(_sub, texto)
        if not trocas:
            continue
        if 'aurea_colors.dart' not in texto:
            linhas = texto.split('\n')
            fim = 0
            for i, l in enumerate(linhas[:80]):
                if l.startswith('import ') or l.startswith("export "):
                    fim = i + 1
                elif l.strip() == '' and fim:
                    break
            linhas.insert(fim, IMPORT)
            texto = '\n'.join(linhas)
        with io.open(caminho, 'w', encoding='utf-8', newline='') as f:
            f.write(texto)
        relatorio.append((caminho, len(trocas)))

for caminho, n in sorted(relatorio):
    print('%3d  %s' % (n, caminho.replace('\\', '/')))
print('---')
print('%d arquivos, %d hexes' % (len(relatorio), sum(n for _, n in relatorio)))
