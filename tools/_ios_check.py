# Verifica a casca SwiftUI do iOS contra o contrato (engine/platform/ios/app/UI_CONTRACT.md).
#
#   python tools/_ios_check.py            # tudo
#   python tools/_ios_check.py textos     # só um bloco
#
# Blocos:
#   textos    toda chave de AureaStrings.t("...") existe no catálogo
#   simbolos  todo método chamado na ponte existe em AureaEngine.h
#   tipos     nenhum tipo Swift declarado duas vezes
#   tokens    nenhuma cor/medida solta (Color(red:), .frame(width: 137) etc.)
#   fluxo     toda tela do Android tem arquivo Swift correspondente
import io
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IOS = ROOT / "engine" / "platform" / "ios"
APP = IOS / "app"
BRIDGE = IOS / "bridge"

IGNORAR = {"AureaStrings.swift"}


def swift_files():
    return sorted(p for p in APP.glob("*.swift") if p.name not in IGNORAR)


def catalogo():
    s = io.open(APP / "AureaStrings.swift", encoding="utf-8").read()
    return set(re.findall(r'"([a-z0-9_]+)"\s*:', s))


def fonte_swift(p):
    s = io.open(p, encoding="utf-8").read()
    # tira comentários de linha e de bloco
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    return re.sub(r"//[^\n]*", "", s)


def bloco_textos():
    chaves = catalogo()
    problemas = []
    usadas = 0
    for p in swift_files():
        for k in re.findall(r'AureaStrings\.t\(\s*"([^"]+)"', fonte_swift(p)):
            usadas += 1
            if k not in chaves:
                problemas.append("%s: chave fora do catálogo: %s" % (p.name, k))
    print("textos:   %d usos, %d chave(s) no catálogo, %d problema(s)" % (usadas, len(chaves), len(problemas)))
    return problemas


def api_ponte():
    h = io.open(BRIDGE / "AureaEngine.h", encoding="utf-8").read()
    h += io.open(BRIDGE / "AureaBridge.h", encoding="utf-8").read()
    return set(re.findall(r"[-+]\s*\([^)]*\)\s*([A-Za-z_][A-Za-z0-9_]*)", h))


def membros_swift():
    """O que a casca pode chamar em `engine`: os métodos da ponte ObjC MAIS os
    membros do AureaModel (a fachada Swift sobre a ponte — `engine.run { }`,
    `engine.text3D`, ... não são chamadas de ponte)."""
    api = api_ponte()
    s = fonte_swift(APP / "AureaModel.swift")
    for nome in re.findall(r"\bfunc\s+([a-zA-Z_][A-Za-z0-9_]*)", s):
        api.add(nome)
    for nome in re.findall(r"\b(?:var|let)\s+([a-zA-Z_][A-Za-z0-9_]*)", s):
        api.add(nome)
    return api


def bloco_simbolos():
    api = membros_swift()
    problemas = []
    usados = 0
    for p in swift_files():
        s = fonte_swift(p)
        for nome in re.findall(r"\bengine\.([a-zA-Z_][A-Za-z0-9_]*)", s):
            usados += 1
            if nome not in api:
                problemas.append("%s: engine.%s não existe na ponte" % (p.name, nome))
    print("simbolos: %d usos de engine.*, %d na ponte, %d problema(s)" % (usados, len(api), len(problemas)))
    return problemas


def bloco_tipos():
    dono = {}
    problemas = []
    for p in swift_files():
        for nome in re.findall(r"^(?:private\s+|internal\s+|public\s+|final\s+)*"
                               r"(?:struct|enum|class|actor)\s+([A-Z][A-Za-z0-9_]*)", fonte_swift(p), flags=re.M):
            if nome in dono and dono[nome] != p.name:
                problemas.append("%s declara %s, que já está em %s" % (p.name, nome, dono[nome]))
            else:
                dono[nome] = p.name
    print("tipos:    %d declarados, %d problema(s)" % (len(dono), len(problemas)))
    return problemas


COR_SOLTA = re.compile(r"Color\(\s*red:|\.foregroundColor\(\s*Color\(\s*red|"
                       r"\.frame\([^)]*\bwidth:\s*\d+(\.\d+)?\s*[,)]|"
                       r"\.frame\([^)]*\bheight:\s*\d+(\.\d+)?\s*[,)]")


def bloco_tokens():
    """Cores e medidas soltas: o desenho do iOS tem que vir dos tokens.

    O que é permitido: `AureaColors.*`, `ShellColors.*`, `AureaDims.*`,
    `ShellDims.*`, `EditorLayoutMetrics.*`, e números em Canvas (geometria de
    desenho, não medida de layout).
    """
    problemas = []
    for p in swift_files():
        for i, linha in enumerate(fonte_swift(p).split("\n"), 1):
            if COR_SOLTA.search(linha):
                problemas.append("%s:%d medida/cor solta: %s" % (p.name, i, linha.strip()[:70]))
    print("tokens:   %d medida(s)/cor(es) solta(s)" % len(problemas))
    return problemas


# Toda tela/painel do Android tem que ter o seu arquivo Swift.
TELAS = {
    "editor/Stage.kt": "Stage.swift",
    "editor/TopBars.kt": "TopBars.swift",
    "editor/Transport.kt": "Transport.swift",
    "editor/BottomArea.kt": "BottomArea.swift",
    "editor/Menus.kt": "Menus.swift",
    "editor/AddLayerPanel.kt": "AddLayerPanel.swift",
    "editor/EditorLayout.kt": "EditorLayout.swift",
    "editor/timeline/TimelineController.kt": "TimelineController.swift",
    "editor/timeline/TimelinePainter.kt": "TimelinePainter.swift",
    "editor/panels/TransformPanel.kt": "PanelTransform.swift",
    "editor/panels/CurvePanel.kt": "PanelCurve.swift",
    "editor/panels/EffectsPanel.kt": "PanelEffects.swift",
    "editor/panels/EffectsBrowser.kt": "EffectsBrowser.swift",
    "editor/panels/PresetsPanel.kt": "PanelPresets.swift",
    "editor/panels/TextPanel.kt": "PanelText.swift",
    "editor/panels/ParticlesPanel.kt": "PanelParticles.swift",
    "editor/panels/Element3DPanel.kt": "Panel3DView.swift",
    "editor/panels/SpeedAudioPanels.kt": "PanelSpeedAudio.swift",
    "editor/panels/VectorPanel.kt": "PanelVector.swift",
    "editor/panels/MaskPanel.kt": "PanelMask.swift",
    "editor/panels/ShapeEditPanel.kt": "PanelShape.swift",
    "editor/panels/ExpressionSheet.kt": "PanelExpression.swift",
    "editor/panels/CaptionsPanel.kt": "PanelCaptions.swift",
    "editor/panels/TrackingPanel.kt": "PanelTracking.swift",
    "editor/panels/AppearancePanel.kt": "PanelAppearance.swift",
    "editor/panels/TimeRemapGraph.kt": "TimeRemapGraph.swift",
    "editor/ExportScreen.kt": "ExportView.swift",
    "editor/ProjectSettingsSheet.kt": "ProjectSettingsSheet.swift",
    "home/StartTab.kt": "HomeTabs.swift",
    "home/ProjectsTab.kt": "HomeTabs.swift",
    "home/SettingsTab.kt": "HomeTabs.swift",
    "home/NewProjectSheet.kt": "NewProjectSheet.swift",
    "home/ProjectCards.kt": "ProjectCards.swift",
}


def bloco_fluxo():
    problemas = []
    for kt, sw in TELAS.items():
        fonte = ROOT / "android/app/src/main/java/com/aurea/aurea" / kt
        if not fonte.exists():
            problemas.append("spec do Android ausente: %s" % kt)
            continue
        if not (APP / sw).exists():
            problemas.append("sem arquivo Swift para %s (esperado %s)" % (kt, sw))
    print("fluxo:    %d tela(s) mapeada(s), %d faltando" % (len(TELAS), len(problemas)))
    return problemas


BLOCOS = {
    "textos": bloco_textos,
    "simbolos": bloco_simbolos,
    "tipos": bloco_tipos,
    "tokens": bloco_tokens,
    "fluxo": bloco_fluxo,
}


def main():
    quais = sys.argv[1:] or list(BLOCOS)
    problemas = []
    for nome in quais:
        if nome not in BLOCOS:
            print("bloco desconhecido: %s" % nome)
            return 2
        problemas += BLOCOS[nome]()
    if problemas:
        print("\nPROBLEMAS (%d):" % len(problemas))
        for p in problemas[:60]:
            print("  " + p)
        return 1
    print("\n0 problema(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
