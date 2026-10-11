"""Synchronize only the 0.0.8 release note entry in the two native catalogs."""
import json
import re
from pathlib import Path
import i18n_add

ROOT = Path(__file__).resolve().parents[1]
NOTES = ROOT / "tools/beta_2147_texts.json"
additions = {
    "pt": "• Modelos glTF respeitam a cena selecionada ao renderizar e calcular o enquadramento.\n• Controles de luz acompanham alterações e desfazer; luzes novas entram no cabeçote.\n• A criação de texturas intermediárias tem um limite de memória por quadro.",
    "en": "• glTF models use the selected scene for rendering and framing.\n• Light controls refresh after changes and undo; new lights start at the playhead.\n• Intermediate texture creation has a per-frame memory limit.",
    "es": "• Los modelos glTF usan la escena seleccionada para renderizar y encuadrar.\n• Los controles de luz se actualizan al editar y deshacer; las luces nuevas comienzan en el cabezal.\n• La creación de texturas intermedias tiene un límite de memoria por cuadro.",
    "ru": "• Модели glTF используют выбранную сцену для рендеринга и кадрирования.\n• Настройки света обновляются после изменений и отмены; новые источники начинаются у указателя.\n• Создание промежуточных текстур ограничено бюджетом памяти кадра.",
    "hi": "• glTF मॉडल रेंडर और फ़्रेमिंग के लिए चुने गए सीन का उपयोग करते हैं।\n• बदलाव और अनडू के बाद लाइट नियंत्रण अपडेट होते हैं; नई लाइट प्लेहेड पर शुरू होती हैं।\n• बीच की टेक्सचर बनाने के लिए हर फ़्रेम की मेमोरी सीमा है।",
    "ar": "• تستخدم نماذج glTF المشهد المحدد للرسم والتأطير.\n• تتحدث أدوات الإضاءة بعد التعديل والتراجع؛ تبدأ الأضواء الجديدة عند المؤشر.\n• يخضع إنشاء القوام الوسيطة لحد ذاكرة لكل إطار.",
    "id": "• Model glTF memakai adegan terpilih untuk rendering dan pembingkaian.\n• Kontrol cahaya diperbarui setelah perubahan dan undo; cahaya baru dimulai di playhead.\n• Pembuatan tekstur perantara memiliki batas memori per frame.",
}
data = json.loads(NOTES.read_text(encoding="utf-8"))
source_path = ROOT / "engine/platform/ios/app/AureaStrings.swift"
source = source_path.read_text(encoding="utf-8")
for lang, extra in additions.items():
    text = data["release_notes_body"][lang]
    if extra not in text:
        text += "\n" + extra
    data["release_notes_body"][lang] = text
    i18n_add.apply(lang, {"release_notes_body": text}, False)
    start = source.index(f"private static let {lang}: [String: String] = [")
    match = re.search(r'^\s*"release_notes_body":\s*"(?:\\.|[^"\\])*",', source[start:], re.M)
    assert match, lang
    row = "\n        \"release_notes_body\": " + json.dumps(text, ensure_ascii=False) + ","
    source = source[:start + match.start()] + row + source[start + match.end():]
source_path.write_text(source, encoding="utf-8")
NOTES.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
print("0.0.8 notes synchronized in seven languages on Android and iOS")
