"""Update the existing activation labels in both native string catalogs."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
LABELS = {
    "pt": "Ativar modo 3D (X/Y/Z)",
    "en": "Enable 3D mode (X/Y/Z)",
    "es": "Activar modo 3D (X/Y/Z)",
    "ru": "Включить режим 3D (X/Y/Z)",
    "hi": "3D मोड चालू करें (X/Y/Z)",
    "ar": "تفعيل الوضع ثلاثي الأبعاد (X/Y/Z)",
    "id": "Aktifkan mode 3D (X/Y/Z)",
}
KEYS = ("panel_girar_3d_x_y", "panel_mostrar_x_y_z_3d")
for language, label in LABELS.items():
    folder = "values" if language == "pt" else "values-" + language
    path = ROOT / "android/app/src/main/res" / folder / "strings.xml"
    text = path.read_text(encoding="utf-8")
    for key in KEYS:
        text, count = re.subn(r'(<string name="' + key + r'">).*?(</string>)',
                              lambda match: match[1] + label + match[2], text)
        assert count == 1, (path, key, count)
    path.write_text(text, encoding="utf-8")

path = ROOT / "engine/platform/ios/app/AureaStrings.swift"
text = path.read_text(encoding="utf-8")
for key in KEYS:
    labels = iter(LABELS.values())
    text, count = re.subn(r'("' + key + r'": ")[^"]*(",)',
                          lambda match: match[1] + next(labels) + match[2], text)
    assert count == len(LABELS), (key, count)
path.write_text(text, encoding="utf-8")
