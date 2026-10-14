"""Check compact sheets; source checks are distinct from Swift/native execution.

On macOS, execute the actual pure sizing helper extracted from Sheets.swift.
Windows checks presentation/scroll/action contracts and explicitly skips Swift.
"""
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[4]
APP = ROOT / 'engine/platform/ios/app'


def block(source, header):
    start = source.index(header)
    opened = source.index('{', start)
    depth = 0
    for index in range(opened, len(source)):
        depth += (source[index] == '{') - (source[index] == '}')
        if depth == 0:
            return source[start:index + 1]
    raise AssertionError(f'Unclosed source block: {header}')


CASES = r'''
var checks = 0
func check(_ condition: Bool, _ label: String) {
    checks += 1
    precondition(condition, label)
}
for viewport: CGFloat in [120, 200, 320, 390, 600, 844, 1024, 1366] {
    for natural: CGFloat in [0, 44, 152, 456, 1200] {
        for footer: CGFloat in [0, 60, 74] {
            let result = AureaSheetLayout.bodyHeight(content: natural, viewport: viewport,
                top: 8, bottom: 34, handle: 21, footer: footer)
            let limit = max(0, viewport - 8 - 34 - 21 - footer)
            check(result >= 0 && result <= limit, "body fits visible viewport")
            check(result <= natural, "short content does not expand")
            if natural < limit { check(result == natural, "natural compact height is preserved") }
        }
    }
}
let full = AureaSheetLayout.bodyHeight(content: 1200, viewport: 760, top: 8, bottom: 34, handle: 21, footer: 60)
let keyboard = AureaSheetLayout.bodyHeight(content: 1200, viewport: 320, top: 8, bottom: 0, handle: 21, footer: 60)
check(keyboard < full && keyboard + 21 + 60 <= 320 - 8, "keyboard reduces body while retaining actions")
check(AureaSheetLayout.bodyHeight(content: -1, viewport: 320, top: 8, bottom: 0) == 0, "negative content stays zero")
check(AureaSheetLayout.bodyHeight(content: 900, viewport: 40, top: 8, bottom: 34, footer: 60) == 0, "actions consume very short viewport")
check(AureaSheetLayout.scrollHeight(content: nil, maximum: 231) == 231, "unmeasured body has a visible first layout")
check(AureaSheetLayout.scrollHeight(content: 149.3, maximum: 231) == 149.3, "measured editor stays compact")
check(AureaSheetLayout.scrollHeight(content: 149.3, maximum: 76) == 76, "landscape scroll fits without hiding fixed actions")
check(AureaSheetLayout.scrollHeight(content: 149.3, maximum: 0) == 0, "a genuinely exhausted viewport remains bounded")
for content: CGFloat in [-1, 0, 20, 96, 150, 180, 3000] {
    let result = AureaSheetLayout.textInputHeight(content: content)
    check(result >= 96 && result <= 180, "text field remains compact and scrolls excess")
}
print("check_sheet_layout: Swift geometry PASS \(checks) checks")
'''


def main():
    sheets = (APP / 'Sheets.swift').read_text(encoding='utf-8')
    editor = (APP / 'EditorView.swift').read_text(encoding='utf-8')
    animation = (APP / 'AnimationTools.swift').read_text(encoding='utf-8')
    property_panel = (APP / 'AureaPropertyPanel.swift').read_text(encoding='utf-8')
    effects = (APP / 'EffectsView.swift').read_text(encoding='utf-8')
    host = (APP / 'ContentView.swift').read_text(encoding='utf-8')
    keypad_host = block(property_panel, '.sheet(item: $keypad)')
    color_host = block(property_panel, '.sheet(item: $picker)')
    marker_host = block(editor, '.sheet(item: $model.markerEditingFrame)')
    about_host = block(effects, '.sheet(item: $about)')
    text = block(editor, 'private struct TextContentEditor: View')
    overlay = block(sheets, 'struct AureaBottomOverlay<Content: View>: View')
    alert = block(sheets, 'struct AureaAlert: View')
    action = block(sheets, 'struct AureaActionSheet: View')
    scroll = block(sheets, 'private struct AureaSheetScroll<Content: View>: View')
    contracts = {
        'text editor is an overlay rather than a forced large native sheet':
            '.sheet(item: $model.textContentRequest)' not in editor and '.id(request.presentationID)' in editor,
        'text input has a measured compact height and native scrolling':
            '.frame(height: inputHeight)' in text and 'scrollable: true' in text and 'AureaSheetLayout.textInputHeight' in text,
        'text actions live outside the scrolling body':
            'fixedFooter: AnyView(actions)' in text and 'private var actions:' in text,
        'text input preserves autofocus, selection and commit callback':
            all(token in text for token in ['autoFocus: true', 'selectAllOnFocus: request.selectAll', 'NSRange', 'model.commitTextContent(request, content: content)', '.disabled(!valid)']),
        'delayed dismissal belongs to the current text presentation':
            'model.textContentRequest?.presentationID == request.presentationID' in text,
        'overlay uses the keyboard-adjusted viewport without subtracting its insets twice':
            'viewport: geometry.size.height' in overlay and 'geometry.safeAreaInsets' not in overlay and 'ignoresSafeArea(.keyboard' not in overlay,
        'overlay deducts measured fixed actions from scroll height':
            'footer: footerHeight' in overlay and 'AureaSheetFooterHeight.self' in overlay,
        'scroll keeps natural short content and limits long content':
            'AureaSheetLayout.scrollHeight(content: contentHeight, maximum: maximumHeight)' in scroll and '.fixedSize(horizontal: false, vertical: true)' in scroll and 'guard $0.isFinite, $0 > 0' in scroll,
        'text sheet identifies its card without replacing identifiers of child actions':
            'accessibilityID: "text.content.sheet"' in text and '.accessibilityElement(children: .contain)' in overlay,
        'long action menus scroll with cancel after scroll':
            action.index('AureaSheetScroll(') < action.index('Text(AureaText.t("common_cancel"))') and 'footer: AureaDims.actionRowMinH' in action,
        'alerts scroll with confirmation outside body and width limited to viewport':
            alert.index('AureaSheetScroll(') < alert.index('HStack(spacing: 0)') and 'geometry.size.width - 32' in alert,
        'color and expression actions use fixed footer':
            'color.picker.done' in sheets and 'AureaBottomOverlay(modal: true, onDismiss: onDismiss, fixedFooter:' in animation,
        'local keypad and color native windows have compact detents and controlled dismissal':
            all(token in keypad_host for token in ['.height(456)', '.presentationDragIndicator(.hidden)', '.interactiveDismissDisabled()'])
            and all(token in color_host for token in ['.height(560)', '.presentationDragIndicator(.hidden)', '.interactiveDismissDisabled()']),
        'scrolling marker and about forms initially allow a medium native window':
            '.presentationDetents([.medium, .large])' in marker_host and '.presentationDetents([.medium, .large])' in about_host,
        'local delayed color callbacks cannot clear a newer request or finish its gesture':
            'guard picker?.id == request.id else { return }' in color_host
            and '.sheet(item: $picker, onDismiss:' not in property_panel
            and 'guard keypad?.id == request.id else { return }' in keypad_host,
        'global color, expression and keypad dismissal belongs to each request':
            all(f'guard model.{name}?.id == request.id else {{ return }}' in host
                for name in ['colorSheet', 'expressionSheet', 'numericKeypad']),
        'compact host modifiers avoid unguarded iOS 16.4 presentation APIs':
            not any(token in keypad_host + color_host + marker_host + about_host
                    for token in ['presentationBackground(', 'presentationCornerRadius(', 'presentationCompactAdaptation(', 'presentationContentInteraction(']),
    }
    for label, passed in contracts.items():
        if not passed:
            raise AssertionError(label)
    print(json.dumps({'source_contracts': len(contracts), 'source': 'PASS', 'native_ui': 'NOT_RUN'}, ensure_ascii=False))
    swiftc = shutil.which('swiftc')
    if not swiftc:
        print('check_sheet_layout: Swift geometry SKIP (swiftc unavailable); iOS UI requires Xcode/simulator')
        return 0
    program = 'import Foundation\n' + block(sheets, 'enum AureaSheetLayout') + '\n' + CASES
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / 'main.swift'
        source.write_text(program, encoding='utf-8')
        executable = Path(directory) / 'sheet-layout'
        subprocess.run([swiftc, str(source), '-o', str(executable)], check=True)
        return subprocess.run([str(executable)]).returncode


if __name__ == '__main__':
    sys.exit(main())
