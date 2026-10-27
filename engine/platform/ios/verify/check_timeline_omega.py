"""Omega 0.3.2 (área 03): regras puras da timeline da referência no iOS.

No Windows confere o contrato no código-fonte (TimelineMath.swift e
TimelineView.swift usam as regras da Omega). Com swiftc (macOS), extrai os
blocos de produção e executa os MESMOS números do teste JVM do Android
(`TimelineOmegaTest.kt`). Nenhum modo é validação nativa de SwiftUI ou em
aparelho.
"""
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
APP = ROOT / "engine/platform/ios/app"


def block(source: str, header: str) -> str:
    start = source.index(header)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise ValueError(f"Bloco Swift sem fim: {header}")


CASES = r'''
var failures = 0
func check(_ ok: Bool, _ message: String) {
    if !ok { failures += 1; print("FAILED: " + message) }
}
// 0.3.4 (bugs-034, cores): cor sólida do tipo, 1/5 dessaturada e 1/8 mais escura
// (os mesmos números do TimelineOmegaTest do Android).
check(ClipColors.solid(0xFF6A52E0) == 0xFF5D4CAF, "solid vídeo")
check(ClipColors.solid(0xFF3D6FD9) == 0xFF3D60AB, "solid imagem")
check(ClipColors.solid(0xFF1F8C93) == 0xFF28757A, "solid áudio")
check(ClipColors.solid(0xFFB07A16) == 0xFF916C26, "solid texto")
check(ClipColors.solid(0xFF2E9459) == 0xFF347B51, "solid forma")
check(ClipColors.solid(0xFFB0417A) == 0xFF8E4068, "solid partículas")
check(ClipColors.solid(0x006A52E0) == 0xFF5D4CAF, "solid opaca")
check(ClipColors.solid(0xFF808080) == 0xFF707070, "solid cinza")
check(ClipColors.mix(0xFF000000, 0xFFFFFFFF, 1, 2) == 0xFF808080, "mix ao meio")
// P1-3: cor do marcador (o motor guarda R no byte baixo).
check(MarkerColors.unpack(0xFFF7C34F) == 0xFF4FC3F7, "padrão do motor é #4FC3F7")
check(MarkerColors.pack(0xFFF16586) == 0xFF8665F1, "rosa empacotado")
for (i, role) in MarkerColors.roles.enumerated() {
    check(MarkerColors.unpack(MarkerColors.pack(role)) == role, "ida e volta \(i)")
    check(MarkerColors.appearance(MarkerColors.pack(role)) == i, "aparência \(i)")
}
check(MarkerColors.appearance(MarkerColors.engineDefault) == 0, "marca nova = rosa")
check(MarkerColors.appearance(0xFF4DB7FF) == 1, "batida laranja = pêssego")
// 0.3.3 recursos: a paleta de seis cores da 0.3.2 com a correção (par do TimelineOmegaTest).
check(MarkerColors.display(MarkerColors.engineDefault) == 0xFFF7C34F, "padrão do motor aparece amarelo")
for (i, argb) in MarkerColors.legacy.enumerated() {
    check(MarkerColors.display(MarkerColors.pack(argb)) == argb, "seis cores: gravada certa \(i)")
    check(MarkerColors.display(argb) == argb, "seis cores: literal cru da 0.3.2 \(i)")
    check(MarkerColors.legacyIndex(MarkerColors.pack(argb)) == i, "seis cores: índice \(i)")
}
check(MarkerColors.display(0xFFFF00FF) == 0xFFFF00FF && MarkerColors.legacyIndex(0xFF123456) == -1, "outra cor")
// P0-5: passo dos tiques.
check(OmegaRuler.frameStep(pps: 100, fps: 30) == 1, "100 pt/s = passo 1")
check(OmegaRuler.frameStep(pps: 10, fps: 30) == 16, "10 pt/s = passo 16")
check(OmegaRuler.frameStep(pps: 800, fps: 30) == 1, "800 pt/s = passo 1")
check(OmegaRuler.secondStep(pps: 3) == 2, "segundos de 2 em 2")
check(OmegaRuler.tickClass(0, fps: 30) == OmegaRuler.secondTick, "quadro 0")
check(OmegaRuler.tickClass(15, fps: 30) == OmegaRuler.halfTick, "meio segundo")
check(OmegaRuler.tickClass(16, fps: 30) == OmegaRuler.frameTick, "quadro")
check(OmegaRuler.frameOf(1, fps: 29.97) == 30, "29,97: segundo 1 no quadro 30")
// P1-5 / P1-6: auto-rolagem.
func near(_ x: CGFloat, _ y: CGFloat) -> Bool { abs(x - y) < 0.001 }
check(OmegaScroll.edgeFactor(pos: 300, from: 200, start: 0, end: 600, zone: 38, intent: 4) == 0, "fora da faixa")
check(near(OmegaScroll.edgeFactor(pos: 563, from: 300, start: 0, end: 600, zone: 38, intent: 4), 1), "1× ao entrar")
check(near(OmegaScroll.edgeFactor(pos: 600, from: 300, start: 0, end: 600, zone: 38, intent: 4), 5), "5× na borda")
check(near(OmegaScroll.edgeFactor(pos: 0, from: 300, start: 0, end: 600, zone: 38, intent: 4), -5), "−5× à esquerda")
check(OmegaScroll.reorderFactor(y: 150, height: 280, rowsTop: 42, zone: 38) == 0, "reordenar: meio")
check(near(OmegaScroll.reorderFactor(y: 60, height: 280, rowsTop: 42, zone: 38), -1), "reordenar: em cima")
check(near(OmegaScroll.reorderFactor(y: 270, height: 280, rowsTop: 42, zone: 38), 1), "reordenar: embaixo")
// P1-1 / P1-7: prazos do toque longo.
check(abs(OmegaHold.base * OmegaHold.fraction(.header, compact: false) - 0.38) < 1e-9, "cabeçalho 95 %")
check(abs(OmegaHold.base * OmegaHold.fraction(.trim, compact: true) - 0.1) < 1e-9, "alça do foco 25 %")
check(abs(OmegaHold.base * OmegaHold.fraction(.key, compact: true) - 0.36) < 1e-9, "losango do foco 90 %")
check(OmegaHold.fraction(.other, compact: false) == 1, "fundo da linha: o toque longo inteiro")
// P1-2: régua navega entre marcadores.
let frames: [Int64] = [10, 40, 90]
let xOf: (Int64) -> CGFloat = { CGFloat($0) * 4 }
check(MarkerNav.target(frames, x: 170, width: 400, playhead: 0, near: 21, xOf: xOf) == 40, "perto do marcador")
check(MarkerNav.target(frames, x: 300, width: 400, playhead: 50, near: 21, xOf: xOf) == 90, "metade direita")
check(MarkerNav.target(frames, x: 100, width: 400, playhead: 50, near: 21, xOf: xOf) == 40, "metade esquerda")
check(MarkerNav.target(frames, x: 300, width: 400, playhead: 95, near: 21, xOf: xOf) == nil, "nada depois")
// P1-5: pulso.
check(LiftPulse.scale(0) == 1 && LiftPulse.scale(0.13) == 1, "pulso volta a 1")
check(abs(LiftPulse.scale(0.065) - 1.04) < 0.0005, "pulso no meio")
// P0-4: reticências no meio.
check(MiddleEllipsis.fit("Camada nova") { $0.count <= 7 } == "Cam\u{2026}ova", "meio")
check(MiddleEllipsis.fit("Camada nova") { _ in false } == "", "nada cabe")
check(omegaMultiSelection(layerSelectMode: true, selected: 0) && omegaMultiSelection(layerSelectMode: false, selected: 2)
      && !omegaMultiSelection(layerSelectMode: false, selected: 1), "seleção múltipla")
if failures > 0 { exit(1) }
print("check_timeline_omega: regras Swift da Omega OK")
'''


def main() -> int:
    math = (APP / "TimelineMath.swift").read_text(encoding="utf-8")
    model = (APP / "TimelineModel.swift").read_text(encoding="utf-8")
    view = (APP / "TimelineView.swift").read_text(encoding="utf-8")
    theme = (APP / "Theme.swift").read_text(encoding="utf-8")
    android = (ROOT / "android/app/src/main/java/com/aurea/aurea/editor/timeline/TimelineMath.kt").read_text(encoding="utf-8")
    checks = {
        "P0-1 uma camada por linha": "timelineLayerRows(all, perLayer: true)" in view,
        "P0-1 reordenar por camada": "timelineGroupKeys(list, byLine: !EditorTimelineRefresh.enabled)" in view,
        "P0-2 régua opaca por cima das linhas": "omegaRows(&context, size: size, now: now)" in view
            and view.index("omegaRows(&context, size: size, now: now)") < view.index("omegaRuler(&context, size: size)"),
        "P0-2 sem sobra embaixo": "var bottomPad: CGFloat { dp(reference ? 0 : 24) }" in model,
        "P0-3 cabeçalho de 62": "static let omegaHeaderColumn: CGFloat = 62" in theme,
        "034 cor sólida do tipo": "ClipColors.solid(row.type.argb)" in view
            and "ClipColors.vary" not in view and "clipGradient" not in theme,
        "P0-5 passo em potência de 2": "OmegaRuler.frameStep(pps: pps, fps: fps)" in view,
        # 0.3.3 recursos: o marcador antigo (0.3.2) voltou — triângulo + nome na régua,
        # sem a zona de toque do relógio, cor pela correção da área 03 (`display`).
        "0.3.3 marcador antigo na régua da Omega": "drawLegacyMarkers(&context, size: size)" in view
            and view.count("drawLegacyMarkers(&context, size: size)") >= 2,
        "0.3.3 relógio sem marcar no toque": "TimelineHit(kind: .currentTime)" not in view,
        "0.3.3 cor do marcador com a correção": "MarkerColors.display(packed)" in view
            and "static func display(_ packed: UInt32) -> UInt32" in math,
        "P1-4 inércia do scrub": "scrubFlinging = true" in view,
        "P1-7 prazo por alvo": "holdDuration: { point in omegaHoldDuration(point, width: size.width) }" in view,
        "P1-8 alça de velocidade": "model.stretchLayerSpan(s.id, start: Int64(s.start), end: Int64(s.end))" in view,
        "Android tem as mesmas regras": all(name in android for name in (
            "object ClipColors", "object MarkerColors", "object OmegaRuler", "object OmegaScroll",
            "object OmegaHold", "object MarkerNav", "object LiftPulse", "object MiddleEllipsis")),
    }
    for label, valid in checks.items():
        if not valid:
            print("FAILED: " + label)
            return 1
    print(f"check_timeline_omega: {len(checks)} checagens de contrato no codigo-fonte OK")
    swiftc = shutil.which("swiftc")
    if not swiftc:
        print("Sem swiftc: regressoes Swift NAO executadas; validacao nativa iOS/aparelho NAO feita.")
        return 0
    program = "\n\n".join([
        "import CoreGraphics\nimport Foundation",
        block(math, "enum TimeAxis {"),
        block(math, "enum ClipColors {"),
        block(math, "enum MarkerColors {"),
        block(math, "enum OmegaRuler {"),
        block(math, "enum OmegaScroll {"),
        block(math, "enum OmegaHold {"),
        block(math, "enum MarkerNav {"),
        block(math, "enum LiftPulse {"),
        block(math, "enum MiddleEllipsis {"),
        "func omegaMultiSelection(layerSelectMode: Bool, selected: Int) -> Bool { layerSelectMode || selected >= 2 }",
        CASES,
    ])
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / "main.swift"
        executable = Path(temporary) / "timeline-omega"
        source.write_text(program, encoding="utf-8")
        result = subprocess.run([swiftc, str(source), "-o", str(executable)])
        if result.returncode:
            return result.returncode
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    sys.exit(main())
