"""Keep the native startup diagnostic equivalent in both string catalogs."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
LABELS = {
    "pt": "O encoder não aceita esta combinação de resolução, FPS e formato. Tente reduzir a resolução ou o FPS.",
    "en": "The encoder cannot accept this resolution, FPS and format combination. Try a lower resolution or FPS.",
    "es": "El codificador no acepta esta combinación de resolución, FPS y formato. Prueba una resolución o FPS menores.",
    "ru": "Кодировщик не поддерживает это сочетание разрешения, FPS и формата. Попробуйте уменьшить разрешение или FPS.",
    "hi": "एनकोडर इस रिज़ॉल्यूशन, FPS और फ़ॉर्मैट के संयोजन को स्वीकार नहीं करता। रिज़ॉल्यूशन या FPS कम करके देखें।",
    "ar": "المُرمِّز لا يقبل هذا الجمع بين الدقة وFPS والصيغة. جرّب دقة أو FPS أقل.",
    "id": "Encoder tidak menerima kombinasi resolusi, FPS dan format ini. Coba resolusi atau FPS lebih rendah.",
}
for language, label in LABELS.items():
    folder = "values" if language == "pt" else "values-" + language
    path = ROOT / "android/app/src/main/res" / folder / "strings_i18n.xml"
    source = path.read_text(encoding="utf-8")
    source, count = re.subn(r'(<string name="eng_encoder_format">).*?(</string>)',
                           lambda match: match[1] + label + match[2], source)
    assert count == 1, (path, count)
    path.write_text(source, encoding="utf-8")
path = ROOT / "engine/platform/ios/app/AureaStrings.swift"
source = path.read_text(encoding="utf-8")
labels = iter(LABELS.values())
source, count = re.subn(r'("eng_encoder_format": ")[^"]*(",)',
                       lambda match: match[1] + next(labels) + match[2], source)
assert count == len(LABELS), count
path.write_text(source, encoding="utf-8")
