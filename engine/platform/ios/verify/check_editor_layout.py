"""Testes da matemática das zonas do editor no iOS (Theme.swift `EditorLayout`).

O alvo do app não tem target de teste de unidade; este script recorta do
Theme.swift as peças PURAS (`SheetContent`, `EditorMetrics`, `EditorLayout` e o
`clamped(to:)`), compila com `swiftc` junto com os casos abaixo e roda. Os casos
são os mesmos do `EditorLayoutTest.kt` do Android: tablet EM PÉ usa o layout do
celular ("a UI inteira sumiu" no iPad 13" era o layout largo em pé), deitado usa
o largo, e todas as zonas ficam visíveis e somam a tela.

Sem `swiftc` (Windows), confere só a regra do `isWide` no texto e avisa que a
execução ficou para o CI (macOS).
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
THEME = ROOT / 'engine/platform/ios/app/Theme.swift'


def block(source: str, header: str) -> str:
    start = source.find(header)
    if start < 0:
        raise SystemExit(f'check_editor_layout: "{header}" sumiu do Theme.swift')
    depth = 0
    for i in range(source.index('{', start), len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[start:i + 1]
    raise SystemExit(f'check_editor_layout: bloco "{header}" sem fim')


CASES = r'''
var failures = 0
func check(_ ok: Bool, _ what: @autoclosure () -> String) {
    if !ok { failures += 1; print("FALHOU: " + what()) }
}
func near(_ a: CGFloat, _ b: CGFloat, _ tol: CGFloat = 0.01) -> Bool { abs(a - b) <= tol }

// Tamanhos em pt da área do editor (sem barras do sistema): iPad mini, Air 11",
// Pro 11", Pro 13" (1024 e 1032 de largura) e um tablet Android grande.
let portraitTablets: [(CGFloat, CGFloat)] = [(744, 1089), (820, 1136), (834, 1150), (800, 1208),
                                              (1024, 1322), (1032, 1332), (1280, 1700)]
for (w, h) in portraitTablets {
    check(!EditorLayout.isWide(w, h), "\(w)x\(h) em pé deveria usar o layout do celular")
    check(EditorLayout.isWide(h, w), "\(h)x\(w) deitado deveria usar o layout largo")
}
check(!EditorLayout.isWide(390, 780), "celular em pé não é largo")
check(EditorLayout.isWide(844, 390), "celular deitado (≥ 600) é largo")
check(EditorLayout.isWide(560, 320), "celular deitado usa folha lateral")
check(!EditorLayout.isWide(1000, 1000), "janela quadrada fica empilhada")
check(EditorLayout.dockQuick >= 44 && EditorLayout.dockTile >= 44, "doca preserva alvos de toque")
// Omega fundação (F6): topo 42, transporte 46, zona fixa de 280 e a prévia igual em todos os estados.
check(near(EditorLayout.topBar, 42) && near(EditorLayout.transport, 46), "barras da referência")
check(near(EditorLayout.bottomZone(), 280) && near(EditorLayout.layerPanel(), 200), "zona de 280 com painel de 200")
let contents: [SheetContent] = [.none, .addBar, .dock, .panel, .curve, .batch, .adding, .hint]
for h in [CGFloat(640), 700, 760, 780, 844, 900, 960] {
    for content in contents {
        let m = EditorLayout.solve(total: h, content: content, fullscreen: false, width: 375, aspect: 9.0 / 16.0)
        check(near(m.preview, h - 368), "\(h) \(content): prévia \(m.preview) deveria ser H - 368")
        check(near(m.total, h), "\(h) \(content): soma \(m.total)")
        check(near(m.zone, 280), "\(h) \(content): zona \(m.zone)")
    }
    let bar = EditorLayout.solve(total: h, content: .addBar, fullscreen: false)
    check(!bar.overlay && near(bar.timeline, 210) && near(bar.sheet, 70), "sem camada: timeline 210 + barra 70")
    // Omega adicionar (área 08, P0-1): adicionando, a barra fica e o conteúdo de 193 fica por cima da timeline.
    let adding = EditorLayout.solve(total: h, content: .adding, fullscreen: false)
    check(!adding.overlay && near(adding.timeline, 210) && near(adding.sheet, 70) && near(EditorLayout.addContent(adding), 193),
          "adicionando: timeline 210 + barra 70, conteúdo de 193 por cima")
    for content in [SheetContent.dock, .panel, .curve] {
        let m = EditorLayout.solve(total: h, content: content, fullscreen: false)
        check(m.overlay && near(m.timeline, 280) && near(m.sheet, 200) && near(m.visibleTimeline, 80),
              "\(content): painel de 200 por cima, 80 de timeline à vista")
    }
    let batch = EditorLayout.solve(total: h, content: .batch, fullscreen: false)
    check(near(batch.sheet, 132) && near(batch.timeline, 148), "seleção múltipla empilhada dentro da zona")
}
let bigFont = EditorLayout.solve(total: 900, content: .panel, fullscreen: false, fontScale: 1.5)
check(near(bigFont.sheet, 220) && near(bigFont.timeline, 300) && near(bigFont.preview, 900 - 388), "fonte 150 %: painel 220, zona 300")
let oneRowDock = EditorLayout.solve(total: 780, content: .dock, fullscreen: false, dockRows: 1)
let twoRowDock = EditorLayout.solve(total: 780, content: .dock, fullscreen: false)
check(near(oneRowDock.sheet, EditorLayout.dockHeight(rows: 1)) && near(oneRowDock.preview, twoRowDock.preview), "doca de uma fileira não mexe na prévia")

for (w, h) in portraitTablets {
    for content in contents {
        let aspects: [CGFloat] = [0, 9.0 / 16.0, 16.0 / 9.0, 1]
        for aspect in aspects {
            let m = EditorLayout.solve(total: h, content: content, fullscreen: false,
                                       width: w - 2 * EditorLayout.previewSideMargin, aspect: aspect)
            let label = "\(w)x\(h) \(content) aspecto \(aspect)"
            check(near(m.topBar, EditorLayout.topBar), "\(label): topo \(m.topBar)")
            check(near(m.transport, EditorLayout.transport), "\(label): transporte \(m.transport)")
            check(m.preview >= EditorLayout.previewMin, "\(label): palco \(m.preview)")
            check(near(m.zone, 280), "\(label): zona \(m.zone)")
            check(near(m.total, h), "\(label): zonas somam \(m.total), tela \(h)")
        }
    }
    // Deitado: a coluna do palco e a folha lateral cabem.
    let timeline = EditorLayout.wideTimeline(w)
    var preview: CGFloat = w - EditorLayout.topBar - EditorLayout.transport
    preview -= EditorLayout.strip + timeline + EditorLayout.addBar
    check(timeline >= 88 && timeline <= 280, "\(h)x\(w) deitado: timeline \(timeline)")
    check(preview >= EditorLayout.previewMin, "\(h)x\(w) deitado: palco \(preview)")
    let sheet = EditorLayout.wideSheetWidth(h)
    check(sheet >= 240 && sheet <= 420 && h - sheet > 400, "\(h)x\(w) deitado: folha \(sheet)")
}
for height in [CGFloat(240), 320, 480, 640, 960] {
    for scale in [CGFloat(1), 1.3, 2] {
        for content in contents {
            let m = EditorLayout.solve(total: height, content: content, fullscreen: false, width: 320, aspect: 16.0 / 9.0, fontScale: scale)
            check(near(m.total, height), "tela curta \(height), fonte \(scale): soma \(m.total)")
            check(m.preview > 0 && m.timeline >= 0 && m.sheet >= 0, "altura negativa em \(height)")
            if m.overlay { check(m.sheet <= m.timeline, "painel maior que a zona em \(height)") }
        }
    }
}
if failures > 0 { print("check_editor_layout: \(failures) falha(s)"); exit(1) }
print("check_editor_layout: OK")
'''


def main() -> int:
    source = THEME.read_text(encoding='utf-8')
    wide = re.search(r'static func isWide\(_ width: CGFloat, _ height: CGFloat\) -> Bool \{\s*(.*?)\s*\}', source, re.S)
    if not wide or re.sub(r'[\s()]+', '', wide.group(1)) != 'width>=520&&width>height&&height<600||width>=900&&width>height':
        print('check_editor_layout: divergência na regra responsiva entre plataformas')
        return 1
    swiftc = shutil.which('swiftc')
    if not swiftc:
        print('check_editor_layout: regra do isWide OK; sem swiftc aqui — os casos rodam no CI (macOS)')
        return 0
    sheet = re.search(r'^enum SheetContent \{[^\n]*\}', source, re.M)
    if not sheet:
        raise SystemExit('check_editor_layout: "enum SheetContent" sumiu do Theme.swift')
    program = '\n\n'.join([
        'import CoreGraphics\nimport Foundation',
        sheet.group(0),
        block(source, 'struct EditorMetrics {'),
        block(source, 'enum EditorLayout {'),
        block(source, 'extension Comparable {'),
        CASES,
    ])
    with tempfile.TemporaryDirectory() as tmp:
        main_swift = Path(tmp) / 'main.swift'
        main_swift.write_text(program, encoding='utf-8')
        exe = Path(tmp) / 'check_editor_layout'
        built = subprocess.run([swiftc, '-O', str(main_swift), '-o', str(exe)])
        if built.returncode != 0:
            print('check_editor_layout: o recorte do Theme.swift não compilou sozinho')
            return built.returncode
        return subprocess.run([str(exe)]).returncode


if __name__ == '__main__':
    sys.exit(main())
