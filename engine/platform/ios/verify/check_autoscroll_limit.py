"""Teto da auto-rolagem durante arraste/aparo (export-falhas.md A.3-1 / C2-1).

No Windows confere só o contrato no código-fonte (TimelineView.swift usa o
GestureLimit em cada ponto da catraca). Com swiftc (macOS), extrai o
GestureLimit e o AutoScroll de produção e executa a mesma simulação do teste
JVM do Android (GestureLimitTest.kt). Nenhum modo é validação nativa de
SwiftUI ou em aparelho.
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
check(GestureLimit.autoScrollLimit(startNavEnd: 900, startDuration: 600, fps: 30) == 1200, "max(conteudo, duracao) + 10 s")
check(GestureLimit.autoScrollLimit(startNavEnd: 900, startDuration: 1200, fps: 60) == 1800, "duracao maior + 10 s a 60 fps")
check(GestureLimit.autoScrollLimit(startNavEnd: 1000, startDuration: 0, fps: 29.97) == 1300, "folga arredonda para cima")
check(GestureLimit.autoScrollLimit(startNavEnd: -5, startDuration: -5, fps: .nan) == 300, "fps invalido = 30")
check(GestureLimit.autoScrollLimit(startNavEnd: Int64(Int32.max), startDuration: 0, fps: 30) == Int64(Int32.max) - 1, "protocolo de frames")
check(GestureLimit.clampView(5000.5, current: 10, limit: GestureLimit.none) == 5000.5, "fora de gesto nada muda")
check(GestureLimit.maxShift(maxEnd: 100, limit: GestureLimit.none) == Int32.max - 1 - 100, "fora de gesto: so o protocolo")
check(GestureLimit.clampView(1250, current: 1190, limit: 1200) == 1200, "vista para no teto")
check(GestureLimit.clampView(1000, current: 1100, limit: 1200) == 1000, "recuar sempre pode")
check(GestureLimit.clampView(1400, current: 1300, limit: 1200) == 1300, "nao salta para tras")
check(GestureLimit.maxShift(maxEnd: 900, limit: 1200) == 300, "deslocamento ate o teto")
check(GestureLimit.maxShift(maxEnd: 1300, limit: 1200) == 0, "lote alem do teto nao anda")
check(GestureLimit.clampFrame(5000, limit: 1200) == 1200, "alvo preso ao teto")

// Dedo PARADO na borda direita movendo um clipe por 10 min a 60 Hz (par do
// GestureLimitTest.kt): a vista também é limitada pelo navigationEnd vivo, que
// passa a ser o fim do próprio clipe.
func hold(_ steps: Int, limited: Bool) -> (end: Int32, view: Double) {
    let ppf = 0.5, halfWidth = 180.0
    let stepPx = Double(AutoScroll.step(factor: 1, maxSpeed: 360 * 2.625, dt: 1.0 / 60))
    let length: Int32 = 300
    var end: Int32 = 900
    var navEnd = end
    let limit = limited ? GestureLimit.autoScrollLimit(startNavEnd: Int64(navEnd), startDuration: 900, fps: 30) : GestureLimit.none
    let maxDelta = GestureLimit.maxShift(maxEnd: end, limit: limit)
    let baseStart = end - length
    var view = 800.0
    let grab = Double(baseStart) - (view + (halfWidth - 10) / ppf)
    for _ in 0..<steps {
        let next = view + stepPx / ppf
        view = min(GestureLimit.clampView(next, current: view, limit: limit), Double(navEnd))
        let desired = Int32(view + (halfWidth - 10) / ppf + grab)
        let target = min(baseStart + maxDelta, max(0, desired))
        end = target + length
        navEnd = end
    }
    return (end, view)
}
let limit = GestureLimit.autoScrollLimit(startNavEnd: 900, startDuration: 900, fps: 30)
let held = hold(60 * 600, limited: true)
check(Int64(held.end) == limit && held.view <= Double(limit), "segurar na borda para no teto (\(held.end), \(held.view))")
check(hold(60 * 600, limited: false).end > 30 * 3600, "controle: sem teto vira catraca")
if failures > 0 { exit(1) }
print("check_autoscroll_limit: regressoes Swift do teto de auto-rolagem OK")
'''


def main() -> int:
    math = (APP / "TimelineMath.swift").read_text(encoding="utf-8")
    view = (APP / "TimelineView.swift").read_text(encoding="utf-8")
    checks = {
        "GestureLimit existe": "enum GestureLimit {" in math,
        "margem fixa de 10 s": "static let marginSeconds: Double = 10" in math,
        "teto capturado no comeco do gesto": "next.limit = GestureLimit.autoScrollLimit(startNavEnd: navEnd, startDuration: model.compositionDuration, fps: model.compositionFps)" in view,
        "auto-rolagem limitada pelo teto do gesto": "GestureLimit.clampView(viewFrame + Double(AutoScroll.step(" in view and "limit: g.limit)" in view,
        "mover clipe: deslocamento preso ao teto": "GestureLimit.maxShift(maxEnd: latest, limit: g.limit)" in view,
        "mover clipe: sem Int32.max - latest": "Int64(Int32.max) - Int64(latest)" not in view,
        "aparar: alvo preso ao teto": view.count("GestureLimit.clampFrame(max(0, snapped == Snap.none ? timelineFrame(desired) : snapped), limit: g.limit)") == 2,
        "Android tem a mesma regra": "object GestureLimit" in (ROOT / "android/app/src/main/java/com/aurea/aurea/editor/timeline/TimelineMath.kt").read_text(encoding="utf-8"),
    }
    for label, valid in checks.items():
        if not valid:
            print("FAILED: " + label)
            return 1
    print(f"check_autoscroll_limit: {len(checks)} checagens de contrato no codigo-fonte OK")
    swiftc = shutil.which("swiftc")
    if not swiftc:
        print("Sem swiftc: regressoes Swift NAO executadas; validacao nativa iOS/aparelho NAO feita.")
        return 0
    program = "\n\n".join([
        "import CoreGraphics\nimport Foundation",
        block(math, "enum AutoScroll {"),
        block(math, "enum GestureLimit {"),
        CASES,
    ])
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / "main.swift"
        executable = Path(temporary) / "autoscroll-limit"
        source.write_text(program, encoding="utf-8")
        result = subprocess.run([swiftc, str(source), "-o", str(executable)])
        if result.returncode:
            return result.returncode
        return subprocess.run([str(executable)]).returncode


if __name__ == "__main__":
    sys.exit(main())
