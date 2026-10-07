"""Check iOS cache-range plumbing; execute real Swift geometry cases on macOS.

Without swiftc this checks source contracts only and explicitly reports that
the Swift cases did not run. It does not validate the native Canvas/Metal UI.
"""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
IOS = ROOT / 'engine/platform/ios'


def block(source: str, header: str) -> str:
    start = source.index(header)
    depth = 0
    for index in range(source.index('{', start), len(source)):
        if source[index] == '{':
            depth += 1
        elif source[index] == '}':
            depth -= 1
            if depth == 0:
                return source[start:index + 1]
    raise ValueError('Unclosed Swift block: ' + header)


CASES = r'''
var failures = 0
func check(_ value: Bool, _ message: String) {
    if !value { failures += 1; print("FAIL: " + message) }
}
func near(_ actual: ClosedRange<CGFloat>?, _ left: CGFloat, _ right: CGFloat) -> Bool {
    guard let actual else { return false }
    return abs(actual.lowerBound - left) < 0.0001 && abs(actual.upperBound - right) < 0.0001
}
check(near(TimelinePreviewBuffer.span(0..<10, view: 0, pxPerFrame: 2, width: 100), 50, 70), "exclusive end")
check(near(TimelinePreviewBuffer.span(0..<10, view: 5, pxPerFrame: 2, width: 100), 40, 60), "scroll follows time axis")
check(near(TimelinePreviewBuffer.span(0..<100, view: 25, pxPerFrame: 2, width: 100), 0, 100), "clip both edges")
check(TimelinePreviewBuffer.span(100..<110, view: 0, pxPerFrame: 2, width: 100) == nil, "offscreen omitted")
let first = TimelinePreviewBuffer.span(0..<2, view: 0, pxPerFrame: 2, width: 100)
let second = TimelinePreviewBuffer.span(5..<7, view: 0, pxPerFrame: 2, width: 100)
check(near(first, 50, 54) && near(second, 60, 64), "uncached gaps remain empty")
check(near(TimelinePreviewBuffer.span(0..<2, view: 0.5, pxPerFrame: 0.25, width: 100), 49.875, 50.375), "fractional zoom")
check(TimelinePreviewBuffer.span(0..<0, view: 0, pxPerFrame: 2, width: 100) == nil, "empty range")
check(TimelinePreviewBuffer.span(-2..<2, view: 0, pxPerFrame: 2, width: 100) == nil, "invalid range")
check(TimelinePreviewBuffer.span(0..<2, view: .nan, pxPerFrame: 2, width: 100) == nil, "invalid view")
check(TimelinePreviewBuffer.span(0..<2, view: 0, pxPerFrame: 0, width: 100) == nil, "invalid scale")
check(TimelinePreviewBuffer.span(0..<2, view: 0, pxPerFrame: 2, width: .infinity) == nil, "invalid viewport")
check(TimelinePreviewBuffer.height == 3, "three-point strip")
if failures != 0 { exit(1) }
print("check_preview_buffer: Swift geometry cases passed")
'''


def main() -> int:
    math = (IOS / 'app/TimelineMath.swift').read_text(encoding='utf-8')
    timeline = (IOS / 'app/TimelineView.swift').read_text(encoding='utf-8')
    model = (IOS / 'app/AureaModel.swift').read_text(encoding='utf-8')
    bridge = (IOS / 'bridge/AureaEngine.mm').read_text(encoding='utf-8')
    theme = (IOS / 'app/Theme.swift').read_text(encoding='utf-8')
    draw = block(timeline, 'private func drawPreviewBuffer(')
    assert 'copy_preview_buffer_ranges(pairs, aurea::kPreviewCacheMaxFrames)' in bridge
    assert 'let pairs = engine.previewBufferRanges()' in model
    assert 'refreshPreviewBufferRanges()' in block(model, 'private func refreshStatus()')
    assert 'for range in model.previewBufferRanges' in draw
    assert 'TimelinePreviewBuffer.span(range' in draw
    assert 'previewBufferStatus' not in draw
    assert 'previewBuffer = Color(hex: 0x4DA3FF)' in theme
    assert 'timeline.preview.buffer' in timeline and '.accessibilityChildren' in timeline
    assert timeline.index('drawPreviewBuffer(&context') < timeline.index('drawPlayhead(&context')
    swiftc = shutil.which('swiftc')
    if not swiftc:
        print('check_preview_buffer: source contract passed; no swiftc, Swift geometry and native UI NOT executed')
        return 0
    program = '\n\n'.join(['import CoreGraphics\nimport Foundation', block(math, 'enum TimeAxis {'),
                            block(math, 'enum TimelinePreviewBuffer {'), CASES])
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'main.swift'
        output = Path(directory) / 'check_preview_buffer'
        source.write_text(program, encoding='utf-8')
        built = subprocess.run([swiftc, '-O', str(source), '-o', str(output)])
        if built.returncode:
            return built.returncode
        return subprocess.run([str(output)]).returncode


if __name__ == '__main__':
    sys.exit(main())
