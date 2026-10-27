# =============================================================================
#  Aurea / platform / ios / verify / check_content_editors.py
#
#  Omega conteúdo (área 06): PARIDADE dos editores de conteúdo (modo de texto,
#  navegador de fontes, desenho, Editar pontos, barra da seção, animadores).
#
#  O que dá para conferir sem o Xcode (as regras de ponto são do motor e têm
#  teste no `aurea_tests`, suíte OmegaContent):
#
#   1. os ids de acessibilidade (testTag do Kotlin x accessibilityIdentifier /
#      tag do Swift) são os MESMOS nas duas plataformas;
#   2. os 25 passos de tamanho e os 42 de espessura são os mesmos;
#   3. as amostras por escrita do navegador de fontes são as mesmas;
#   4. a numeração das operações de ponto (PathOps) bate com Engine::PathOp;
#   5. com `swiftc` (macOS), roda as contas puras do Swift com os casos do
#      OmegaContentLogicTest.kt (busca, amostra, passos).
#
#  Isto é auditoria de fonte: não comprova a execução nativa das interfaces.
# =============================================================================
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
APP = ROOT / 'engine/platform/ios/app'
KT = ROOT / 'android/app/src/main/java/com/aurea/aurea/editor'
ENGINE_HPP = ROOT / 'engine/include/aurea/Engine.hpp'

SWIFT_FILES = ['TextEditModeView.swift', 'FontBrowserView.swift', 'DrawingEditorView.swift', 'EditPointsView.swift',
               'OmegaContent.swift', 'LayerPanels.swift']
KT_FILES = ['TextMode.kt', 'FontBrowser.kt', 'DrawingEditor.kt', 'EditPoints.kt', 'OmegaContent.kt', 'ContentColorPicker.kt',
            'panels/TextAnimSection.kt']
PREFIXES = ('text.style.', 'text.edit.', 'text.font.', 'text.popup.', 'text.size.', 'text.color.', 'font.', 'draw.',
            'points.', 'shape.lockAspect', 'shape.fromCenter', 'anim.')
# Ids que só um lado tem de propósito (o palco do iOS é uma camada SwiftUI própria;
# no Android o palco é o Canvas do Stage.kt e a alça de largura é desenhada).
ONLY_IOS = {'stage.drawing', 'stage.points', 'text.stage.boxHandle'}

problems = []


def read(path):
    return path.read_text(encoding='utf-8')


def ids(text):
    found = set()
    for literal in re.findall(r'"((?:[a-z]+\.)+[a-zA-Z0-9_.$(){}\\ +\-]*)"', text):
        norm = re.sub(r'\$\{[^}]*\}|\$[a-zA-Z_]+|\\\([^)]*\)', '*', literal)
        if norm.startswith(PREFIXES):
            found.add(norm)
    return found


def int_array(text, pattern):
    m = re.search(pattern, text, re.S)
    if not m:
        return None
    return [int(x) for x in re.findall(r'\d+', m.group(1))]


def main():
    swift = '\n'.join(read(APP / f) for f in SWIFT_FILES)
    kotlin = '\n'.join(read(KT / f) for f in KT_FILES)

    # 1) Ids.
    s_ids, k_ids = ids(swift), ids(kotlin)
    # O "font.item.*" do Swift junta a chave; no Kotlin o "$tag" é "font.item.<chave>".
    k_ids = {i.replace('*.fav', 'font.item.*.fav').replace('*.imported', 'font.item.*.imported') if i.startswith('*') else i for i in k_ids}
    only_k = sorted(i for i in k_ids - s_ids if i not in ONLY_IOS)
    only_s = sorted(i for i in s_ids - k_ids if i not in ONLY_IOS)
    if only_k:
        problems.append('ids só no Android: ' + ', '.join(only_k))
    if only_s:
        problems.append('ids só no iOS: ' + ', '.join(only_s))

    # 2) Passos.
    k_size = int_array(read(KT / 'TextMode.kt'), r'TextSizeSteps = intArrayOf\((.*?)\)')
    s_size = int_array(read(APP / 'TextEditModeView.swift'), r'static let values: \[Int\] = \[(.*?)\]')
    if k_size != s_size or not k_size or len(k_size) != 36:
        problems.append('passos de tamanho divergentes: %s x %s' % (k_size, s_size))
    k_width = int_array(read(KT / 'DrawingEditor.kt'), r'DrawWidthSteps = intArrayOf\((.*?)\)')
    s_width = int_array(read(APP / 'DrawingEditorView.swift'), r'static let values: \[Int\] = \[(.*?)\]')
    if k_width != s_width or not k_width or len(k_width) != 42:
        problems.append('passos de espessura divergentes: %s x %s' % (k_width, s_width))

    # 3) Amostras por escrita.
    k_samples = re.findall(r'(GREEK|CYRILLIC|HEBREW|ARABIC|THAI|KOREAN|JAPANESE|CHINESE|DEVANAGARI) -> "([^"]+)"', read(KT / 'FontBrowser.kt'))
    s_samples = re.findall(r'case (greek|cyrillic|hebrew|arabic|thai|korean|japanese|chinese|devanagari): own = "([^"]+)"', read(APP / 'FontBrowserView.swift'))
    if sorted((a.lower(), b) for a, b in k_samples) != sorted(s_samples) or len(s_samples) != 9:
        problems.append('amostras por escrita divergentes')

    # 4) PathOps x Engine::PathOp.
    hpp = read(ENGINE_HPP)
    m = re.search(r'enum class PathOp : u32 \{(.*?)\};', hpp, re.S)
    engine_ops = dict((name, int(value)) for name, value in re.findall(r'(\w+)\s*=\s*(\d+)', m.group(1))) if m else {}
    swift_ops = dict((name, int(value)) for name, value in re.findall(r'static let (\w+): UInt32 = (\d+)', read(APP / 'EditPointsView.swift')))
    kotlin_ops = dict((name, int(value)) for name, value in re.findall(r'const val (\w+) = (\d+)', read(KT / 'EditPoints.kt').split('object PathOps')[1].split('}')[0]))
    want = {'MovePoints': 0, 'MoveHandle': 1, 'Insert': 2, 'Remove': 3, 'SetType': 4, 'Append': 5, 'SetClosed': 6, 'SetExclude': 7}
    if engine_ops != want:
        problems.append('Engine::PathOp mudou: %s' % engine_ops)
    if sorted(swift_ops.values()) != list(range(8)) or swift_ops.get('setExclude') != 7 or swift_ops.get('append') != 5:
        problems.append('PathOps do Swift fora da numeração do motor: %s' % swift_ops)
    if sorted(kotlin_ops.values()) != list(range(8)) or kotlin_ops.get('SET_EXCLUDE') != 7 or kotlin_ops.get('APPEND') != 5:
        problems.append('PathOps do Kotlin fora da numeração do motor: %s' % kotlin_ops)

    print(json.dumps({'ids_ios': len(s_ids), 'ids_android': len(k_ids), 'problems': len(problems), 'native_ui': 'NOT_RUN'}, ensure_ascii=False))
    for p in problems:
        print('   ' + p)

    # 5) Contas puras no Swift (só com swiftc).
    swiftc = shutil.which('swiftc')
    if not swiftc:
        print('check_content_editors: Swift logic SKIP (swiftc unavailable)')
        return 1 if problems else 0
    browser = read(APP / 'FontBrowserView.swift')
    textmode = read(APP / 'TextEditModeView.swift')
    drawing = read(APP / 'DrawingEditorView.swift')

    def block(source, header):
        start = source.index(header)
        opened = source.index('{', start)
        depth = 0
        for index in range(opened, len(source)):
            depth += (source[index] == '{') - (source[index] == '}')
            if depth == 0:
                return source[start:index + 1]
        raise AssertionError(header)

    program = '\n'.join([
        'import Foundation',
        block(browser, 'enum FontScripts'),
        block(browser, 'func fontMatches'),
        block(textmode, 'enum TextSizeSteps'),
        block(drawing, 'enum DrawWidthSteps'),
        '''
precondition(fontMatches("Roboto Mono", "ro") == 0)
precondition(fontMatches("Roboto Mono", "mo") == 7)
precondition(fontMatches("Roboto Mono", "bo") == -1)
precondition(fontMatches("Roboto Mono", "") == 0)
precondition(fontMatches("Roboto Mono", "MONO") == 7)
precondition(FontScripts.sample(FontScripts.latin, long: true) == "Abcdefg")
precondition(FontScripts.sample(FontScripts.latin | FontScripts.greek | FontScripts.cyrillic, long: false) == "Abc")
precondition(FontScripts.sample(FontScripts.latin | FontScripts.greek | FontScripts.japanese | FontScripts.chinese, long: false) == "あいう")
precondition(FontScripts.sample(FontScripts.latin | FontScripts.arabic, long: true).hasSuffix("Abcd"))
precondition(TextSizeSteps.values.count == 25)
precondition(TextSizeSteps.values[TextSizeSteps.nearest(72)] == 72)
precondition(TextSizeSteps.values[TextSizeSteps.nearest(900)] == 144)
precondition(TextSizeSteps.values[TextSizeSteps.nearest(1)] == 5)
precondition(DrawWidthSteps.values.count == 42 && DrawWidthSteps.values.first == 1 && DrawWidthSteps.values.last == 300)
print("check_content_editors: Swift logic PASS")
''',
    ])
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'main.swift'
        source.write_text(program, encoding='utf-8')
        executable = Path(directory) / 'content-logic'
        subprocess.run([swiftc, str(source), '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)]).returncode
    return 1 if problems or result else 0


if __name__ == '__main__':
    sys.exit(main())
