"""Omega 0.3.2 (área 07): o painel de efeitos do iOS contra o contrato.

No Windows confere o contrato no código-fonte: as constantes do `FxParamMath`
Swift (EffectsHuman.swift) são as mesmas do Kotlin (ParamRowMath.kt), o painel
usa a moldura comum e a regra de abrir o efeito novo, os ids da lista, dos
controles, das linhas e dos menus são os do Android, e o arrasto relativo do
preview fica abaixo das alças das ferramentas. Com swiftc (macOS), extrai o
`FxParamMath` de produção e executa os MESMOS números do teste JVM do Android
(`FxParamMathTest.kt`). Nenhum modo é validação nativa de SwiftUI ou em aparelho.
"""
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
APP = ROOT / "engine/platform/ios/app"
KOTLIN = ROOT / "android/app/src/main/java/com/aurea/aurea/ui/ds/ParamRowMath.kt"


def block(source: str, header: str) -> str:
    start = source.index(header)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[start:index + 1]
    raise ValueError(f"Bloco Swift sem fim: {header}")


CONSTANTS = ("ROW_SLIDER", "ROW_RULER", "ROW_SMALL", "ROW_SECTION", "ROW_LIST", "LIST_GAP", "LABEL_W",
             "VALUE_W", "LABEL_MARGIN", "TICK_SPACING", "MAJOR_EVERY", "FLING_MIN_PX_S",
             "RULER_SLOP_FRACTION", "SLIDER_SLOP_FRACTION", "SLIDER_SNAP_DP", "OPEN_MS", "SECTION_ARROW_MS")


def constants_match(swift: str, kotlin: str) -> list:
    problems = []
    for name in CONSTANTS:
        k = re.search(r"const val %s\s*=\s*([0-9.]+)f?" % name, kotlin)
        s = re.search(r"static let %s(?::\s*\w+)?\s*=\s*([0-9.]+)" % name, swift)
        if not k or not s:
            problems.append("constante ausente: %s" % name)
        elif float(k.group(1)) != float(s.group(1)):
            problems.append("%s: Kotlin %s != Swift %s" % (name, k.group(1), s.group(1)))
    for name in ("rowHeight", "controlFor", "sliderValueAt", "sliderX", "rulerValueAfter", "snapToTick",
                 "limitRange", "crossedSnap", "decimalsForStep", "angleTurns", "opensAfterAdd", "trackball",
                 "eulerToQuat", "quatToEuler", "alphaAt"):
        if not re.search(r"\bfun %s\b" % name, kotlin):
            problems.append("Kotlin sem %s" % name)
        if not re.search(r"\bfunc %s\b" % name, swift):
            problems.append("Swift sem %s" % name)
    return problems


CASES = r'''
var failures = 0
func check(_ ok: Bool, _ message: String) {
    if !ok { failures += 1; print("FAILED: " + message) }
}
func near(_ a: Float, _ b: Float, _ e: Float = 1e-6) -> Bool { abs(a - b) <= e }
func near(_ a: Double, _ b: Double, _ e: Double) -> Bool { abs(a - b) <= e }
// Alturas e controle (FxParamMathTest.rowHeightsFollowTheReference).
// 0.3.4 painéis: as linhas cresceram (eram 51 / 57 / 38).
check(FxParamMath.rowHeight(FxParamControl.slider, fxParamFloat) == 60, "slider 60")
check(FxParamMath.rowHeight(FxParamControl.ruler, fxParamFloat) == 66, "régua 66")
check(FxParamMath.rowHeight(FxParamControl.point, fxParamPoint2D) == 66, "ponto 66")
check(FxParamMath.rowHeight(FxParamControl.xyz, fxParamPoint3D) == 66, "xyz 66")
check(FxParamMath.rowHeight(FxParamControl.auto, fxParamBool) == 46, "interruptor 46")
check(FxParamMath.rowHeight(FxParamControl.auto, fxParamEnum) == 46, "escolha 46")
check(FxParamMath.rowHeight(FxParamControl.auto, fxParamFloat) == 66, "número antigo = régua")
check(FxParamMath.controlFor(FxParamControl.auto, fxParamAngle) == FxParamControl.ruler, "ângulo = régua")
check(FxParamMath.controlFor(FxParamControl.auto, fxParamPoint2D) == FxParamControl.point, "ponto")
check(FxParamMath.controlFor(FxParamControl.auto, fxParamPoint3D) == FxParamControl.xyz, "xyz")
check(FxParamMath.controlFor(FxParamControl.slider, fxParamFloat) == FxParamControl.slider, "do motor")
check(FxParamMath.controlFor(FxParamControl.auto, fxParamColor) == FxParamControl.auto, "cor sem controle")
// Slider (sliderIsAbsoluteQuantizedAndHoldsInTheSnapZone).
let w: Float = 210
check(FxParamMath.sliderValueAt(x: 0, width: w, min: 0, max: 100, step: 1, snaps: [50], snapPx: 10) == 0, "slider 0")
check(FxParamMath.sliderValueAt(x: w, width: w, min: 0, max: 100, step: 1, snaps: [50], snapPx: 10) == 100, "slider fim")
check(FxParamMath.sliderValueAt(x: 50, width: w, min: 0, max: 100, step: 1, snaps: [50], snapPx: 10) == 25, "slider 25")
check(FxParamMath.sliderValueAt(x: 100, width: w, min: 0, max: 100, step: 1, snaps: [50], snapPx: 10) == 50, "pista início")
check(FxParamMath.sliderValueAt(x: 109, width: w, min: 0, max: 100, step: 1, snaps: [50], snapPx: 10) == 50, "pista fim")
check(FxParamMath.sliderValueAt(x: 160, width: w, min: 0, max: 100, step: 1, snaps: [50], snapPx: 10) == 75, "depois da pista")
check(FxParamMath.sliderValueAt(x: 61, width: 200, min: 0, max: 100, step: 10, snaps: [], snapPx: 10) == 30, "quantizado")
check(FxParamMath.sliderX(value: 50, width: w, min: 0, max: 100, snaps: [50], snapPx: 10) == 105, "polegar no meio da pista")
check(FxParamMath.sliderX(value: 75, width: w, min: 0, max: 100, snaps: [50], snapPx: 10) == 160, "polegar depois da pista")
check(FxParamMath.sliderValueAt(x: 100, width: 200, min: 0, max: 100, step: 0, snaps: [-5, 100], snapPx: 10) == 50, "encaixe fora")
// Régua (rulerTickIsOneStepAndSnapsAndLimits).
check(near(FxParamMath.rulerValueAfter(start: 2, scrolledPx: 22, spacingPx: 11, step: 0.5), 3), "22 px = +1")
check(near(FxParamMath.rulerValueAfter(start: 2, scrolledPx: -11, spacingPx: 11, step: 0.5), 1.5), "−11 px = −0,5")
check(FxParamMath.rulerValueAfter(start: 2, scrolledPx: 22, spacingPx: 0, step: 0.5) == 2, "sem espaçamento")
check(near(FxParamMath.snapToTick(1.6, step: 0.5), 1.5), "encaixa 1,5")
check(near(FxParamMath.snapToTick(-1.8, step: 1), -2), "encaixa −2")
check(FxParamMath.limitRange(12, min: 0, max: 10) == 10, "prende no máx")
check(FxParamMath.limitRange(-3, min: 10, max: 0) == 0, "faixa invertida")
check(FxParamMath.crossedSnap(from: -0.5, to: 0.5, snaps: [0]), "cruzou o zero")
check(FxParamMath.crossedSnap(from: 0.5, to: 0, snaps: [0]), "chegou ao zero")
check(!FxParamMath.crossedSnap(from: 0, to: 0.5, snaps: [0]), "saindo do encaixe não vibra")
check(!FxParamMath.crossedSnap(from: 1, to: 2, snaps: [0]), "longe do encaixe")
// Formato (decimalsFollowTheStepAndAnglesShowTurns).
check(FxParamMath.decimalsForStep(0.00001) == 5, "5 casas")
check(FxParamMath.decimalsForStep(0.005) == 3, "3 casas")
check(FxParamMath.decimalsForStep(0.01) == 2, "2 casas")
check(FxParamMath.decimalsForStep(0.5) == 1, "1 casa")
check(FxParamMath.decimalsForStep(1) == 0, "0 casas")
check(FxParamMath.angleTurns(270) == (0, 270), "270 sem voltas")
check(FxParamMath.angleTurns(750) == (2, 30), "750 = 2× + 30")
check(FxParamMath.angleTurns(-750) == (-2, -30), "−750 = −2× − 30")
// Lista (onlyTheFirstEffectOfAnEmptyLayerOpensItself).
check(FxParamMath.opensAfterAdd(previousCount: 0, addedCount: 1), "1º efeito abre")
check(!FxParamMath.opensAfterAdd(previousCount: 1, addedCount: 1), "2º não abre")
check(!FxParamMath.opensAfterAdd(previousCount: 0, addedCount: 0), "nada adicionado")
// Guia (guideTimingsFadeInHoldAndOut).
let s = FxParamMath.Guide.short
check(s.total == 1500, "curto 1500")
check(near(s.alphaAt(50), 0.5), "entrando")
check(near(s.alphaAt(300), 1), "parado")
check(near(s.alphaAt(100 + 500 + 450), 0.5), "saindo")
check(near(s.alphaAt(2000), 0), "fim")
check(FxParamMath.Guide.long.total == 4100, "longo 4100")
check(FxParamMath.Guide.medium.total == 2400, "médio 2400")
// Trackball (trackballRoundTripsAndRotatesAroundTheRightAxis).
let q = FxParamMath.eulerToQuat(20, -35, 50)
let e = FxParamMath.quatToEuler(q)
check(near(e[0], 20, 1e-6) && near(e[1], -35, 1e-6) && near(e[2], 50, 1e-6), "ida e volta")
let same = FxParamMath.trackball(alpha: 10, beta: 20, gamma: 30, x0: 0.2, y0: 0.1, x1: 0.2, y1: 0.1)
check(near(same[0], 10, 1e-4) && near(same[1], 20, 1e-4) && near(same[2], 30, 1e-4), "sem arrasto")
let yaw = FxParamMath.trackball(alpha: 0, beta: 0, gamma: 0, x0: 0, y0: 0, x1: 0.5, y1: 0)
check(abs(yaw[1]) > 20 && near(yaw[0], 0, 1e-3), "horizontal = guinada")
let pitch = FxParamMath.trackball(alpha: 0, beta: 0, gamma: 0, x0: 0, y0: 0, x1: 0, y1: 0.5)
check(abs(pitch[0]) > 20 && near(pitch[1], 0, 1e-3), "vertical = inclinação")
// Seções (P1-4) — os mesmos casos do EffectSectionsTest (JVM).
func slot(_ index: UInt32, _ type: Int = fxParamFloat, section: Int = 0, toggle: Int = -1, open: Bool = true) -> EffectParamItem {
    EffectParamItem(flags: 1, index: index, type: UInt32(type), label: "p\(index)", unit: "", paramId: "",
                    value: [0, 0, 0, 0], defaultValue: [0, 0, 0, 0], minValue: 0, maxValue: 1, hardMin: 0, hardMax: 1,
                    animated: false, enumLabels: [], section: section, sectionId: section > 0 ? "s\(section)" : "",
                    sectionLabel: section > 0 ? "Seção \(section)" : "", sectionToggle: toggle, sectionOpen: open)
}
let plain = (0..<4).map { slot(UInt32($0)) }
let plainLayout = fxEffectLayout(0x1234, plain)
check(plainLayout.main == plain && plainLayout.advanced.isEmpty && plainLayout.sections.isEmpty, "sem seção: tudo à vista")
let mixed = [slot(0, fxParamBool, section: 1, toggle: 0), slot(5, section: 2, open: false), slot(3, section: 1, toggle: 0),
             slot(1, section: 1, toggle: 0), slot(2), slot(4, section: 2, open: false), slot(6)]
let layout = fxEffectLayout(0x1234, mixed)
check(layout.main.map(\.index) == [2, 6] && layout.advanced.isEmpty, "soltas primeiro")
check(layout.sections.map(\.ordinal) == [1, 2], "seções pelo ordinal")
check(layout.sections[0].toggle?.index == 0 && layout.sections[0].params.map(\.index) == [1, 3] && layout.sections[0].open, "seção 1")
check(layout.sections[0].label == "Seção 1", "título da seção")
check(layout.sections[1].toggle == nil && !layout.sections[1].open && layout.sections[1].params.map(\.index) == [4, 5], "seção 2")
check(layout.all.map(\.index) == [2, 6, 0, 1, 3, 4, 5], "ordem da tela")
check(fxClosedSection(layout, param: 5) == 2 && fxClosedSection(layout, param: 3) == 0 && fxClosedSection(layout, param: 2) == 0, "seção recolhida")
let onlyToggle = fxEffectLayout(0x1234, [slot(0, fxParamBool, section: 1, toggle: 0), slot(1)])
check(onlyToggle.sections.isEmpty && onlyToggle.main.map(\.index) == [1], "seção só com o interruptor some")
let odd = fxEffectLayout(0x1234, [slot(0, fxParamFloat, section: 1, toggle: 0), slot(1, section: 1, toggle: 0)])
check(odd.sections.count == 1 && odd.sections[0].toggle == nil && odd.sections[0].params.map(\.index) == [1], "interruptor não Bool")
check(fxStagePointEligible(type: fxParamPoint2D, flags: 1 | 8) && fxStagePointEligible(type: fxParamPoint3D, flags: 1 | 8), "ponto relativo")
check(!fxStagePointEligible(type: fxParamFloat, flags: 1 | 8) && !fxStagePointEligible(type: fxParamPoint2D, flags: 1 | 8 | 16), "não é ponto")
check(!fxSwipeCommits(dx: -100, width: 400, minimum: 96) && fxSwipeCommits(dx: -140, width: 400, minimum: 96), "35 %")
check(!fxSwipeCommits(dx: 200, width: 400, minimum: 96), "direita não apaga")
check(!fxSwipeCommits(dx: -60, width: 100, minimum: 96) && fxSwipeCommits(dx: -96, width: 100, minimum: 96), "mínimo")
if failures > 0 { exit(1) }
print("check_effects_omega: FxParamMath e fxEffectLayout Swift = FxParamMathTest/EffectSectionsTest (JVM) OK")
'''


def main() -> int:
    human = (APP / "EffectsHuman.swift").read_text(encoding="utf-8")
    view = (APP / "EffectsView.swift").read_text(encoding="utf-8")
    design = (APP / "DesignSystem.swift").read_text(encoding="utf-8")
    stage = (APP / "SaberStage.swift").read_text(encoding="utf-8")
    editor = (APP / "EditorView.swift").read_text(encoding="utf-8")
    model = (APP / "AureaModel.swift").read_text(encoding="utf-8")
    kotlin = KOTLIN.read_text(encoding="utf-8")
    math = block(human, "enum FxParamMath {")
    problems = constants_match(math, kotlin)
    ids = (
        "effects.settings.", "effects.settings.header", "effects.settings.body", "effects.more.", "effects.remove.",
        "effects.expand.", "effects.eye.", "effects.grip.", "aurea.effects.add", "effects.add.row",
        "aurea.effects.stack", "effects.tool.", "effects.tool.remove.", "effects.param.", "effects.slider.",
        "effects.ruler.", "effects.vector.", "effects.axis.", "effects.toggle.", "effects.choice.",
        "effects.color.", "effects.variation.", "effects.section.", "effects.section.toggle.",
        "effects.tip.", "effects.menu", "effects.menu.details", "effects.menu.reset", "effects.menu.duplicate",
        "effects.menu.copy", "effects.menu.paste_settings", "effects.menu.expression", "effects.menu.save_preset",
        "effects.menu.turbulence", "effects.list.copy", "effects.list.paste", "effects.list.toggle_all",
        "effects.list.create", "effects.list.library", "effects.list.import", "effects.save", "effects.save.confirm",
    )
    problems += ["EffectsView sem o id %s" % i for i in ids if '"' + i not in view]
    if '.advanced"' not in view:
        problems.append("EffectsView sem o id effects.section.<e>.advanced")
    checks = {
        "P0-2 moldura comum, lista própria": "PanelFrame(rail: panelRail, scrollContent: false)" in view,
        "P0-2 ⋯ inativo com os controles abertos": "moreEnabled: openId == nil" in view,
        "P0-2 só o 1º efeito de camada vazia abre": "FxParamMath.opensAfterAdd(previousCount: seen.count" in view,
        "P0-2 decorações fora da lista": "fxShownInEffectList($0)" in view and "!e.isDecoration" in human,
        "P0-2 toque longo de 0,4 s reordena": "LongPressGesture(minimumDuration: 0.4)" in view,
        "P0-3 abertura de 200 ms acelera-desacelera": "timingCurve(0.42, 0, 0.58, 1, duration: Double(FxParamMath.OPEN_MS)" in view,
        "P0-3 recorte + deslocamento": ".mask(alignment: .top)" in view and "overlayFrom * (1 - overlayProgress)" in view,
        "P0-4 controles da referência": all(n in design for n in ("struct FxParamLabel", "struct FxValueBox",
                                                                   "struct AureaSlider", "struct FxRuler",
                                                                   "struct FxSegmented", "struct FxDropdownBox")),
        "P1-1 régua anda de risco em risco": "FxParamMath.snapToTick(raw, step: tickStep)" in design,
        "P1-1 arremesso ignora toque em outra régua": "FxRulerFling.owner" in design,
        "P1-1 vibração leve 0,2": "impactOccurred(intensity: 0.2)" in design,
        "P1-2 guia e arrasto relativo": "stage.effect.guide" in stage and "FxParamMath.trackball(" in stage,
        "P1-2 arrasto abaixo das alças": "EffectParamDragOverlay()" in editor
            and editor.index("EffectParamDragOverlay()") < editor.index("RigStageOverlay()"),
        "P1-3 toque longo zera o eixo": "om_fx_axis_reset" in view,
        "P1-5 ◆± rola até a linha": "proxy.scrollTo(\"fx.param." in view,
        "P2-3 casas pelo passo declarado": "rowFlags & 4 != 0" in human,
        "ponte: controle/passo/estilo/habilitação": all(n in model for n in ("var control", "var step", "var origin",
                                                                              "var enumStyle", "var rowFlags")),
    }
    problems += ["FAILED: " + label for label, ok in checks.items() if not ok]
    if problems:
        for p in problems:
            print(p)
        return 1
    print("check_effects_omega: %d constantes iguais ao Kotlin, %d ids e %d checagens de contrato OK"
          % (len(CONSTANTS), len(ids), len(checks)))
    swiftc = shutil.which("swiftc")
    if not swiftc:
        print("Sem swiftc: regressoes Swift NAO executadas; validacao nativa iOS/aparelho NAO feita.")
        return 0
    program = "\n\n".join([
        "import CoreGraphics\nimport Foundation",
        block(model, "enum FxParamControl {"),
        "\n".join(line for line in human.splitlines() if re.match(r"let fxParam\w+ = \d+$", line)),
        math,
        block(model, "struct EffectParamItem: Identifiable, Equatable {"),
        block(human, "func fxComponentCount("),
        block(human, "struct FxParamSlot: Equatable {"),
        block(human, "struct FxSection: Identifiable {"),
        block(human, "struct FxLayout {"),
        # A tabela humana fica fora: sem tabela, a regra padrão (até 6 à vista; acima, os 5 primeiros).
        "func fxSplitPrincipal(_ typeId: UInt32, _ visible: [FxParamSlot]) -> (main: [FxParamSlot], rest: [FxParamSlot]) {"
        " visible.count <= 6 ? (visible, []) : (Array(visible.prefix(5)), Array(visible.dropFirst(5))) }",
        block(human, "func fxEffectLayout("),
        block(human, "func fxClosedSection("),
        block(human, "func fxStagePointEligible("),
        block(human, "func fxSwipeCommits("),
        CASES,
    ])
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / "main.swift"
        executable = Path(temporary) / "effects-omega"
        source.write_text(program, encoding="utf-8")
        result = subprocess.run([swiftc, str(source), "-o", str(executable)])
        if result.returncode:
            return result.returncode
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    sys.exit(main())
