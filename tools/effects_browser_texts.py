"""Keep the effects browser labels in sync across the two native apps."""
from pathlib import Path
import json
from xml.sax.saxutils import escape

root = Path(__file__).resolve().parents[1]
keys = ['all', 'new', 'badge', 'count', 'before', 'after', 'timeline_preview', 'loading', 'preview_unavailable']
translations = {
 'pt': ['Todos', 'Novos', 'Novo', '%d efeitos', 'Antes', 'Depois', 'Veja na timeline', 'Preparando prévia…', 'Prévia indisponível'],
 'en': ['All', 'New', 'New', '%d effects', 'Before', 'After', 'View on timeline', 'Preparing preview…', 'Preview unavailable'],
 'es': ['Todos', 'Nuevos', 'Nuevo', '%d efectos', 'Antes', 'Después', 'Ver en la línea de tiempo', 'Preparando vista previa…', 'Vista previa no disponible'],
 'ru': ['Все', 'Новые', 'Новое', 'Эффектов: %d', 'До', 'После', 'Смотреть на шкале времени', 'Подготовка просмотра…', 'Просмотр недоступен'],
 'hi': ['सभी', 'नए', 'नया', '%d प्रभाव', 'पहले', 'बाद में', 'टाइमलाइन पर देखें', 'पूर्वावलोकन तैयार हो रहा है…', 'पूर्वावलोकन उपलब्ध नहीं है'],
 'ar': ['الكل', 'الجديد', 'جديد', '%d تأثيرات', 'قبل', 'بعد', 'عرض على المخطط الزمني', 'جارٍ تحضير المعاينة…', 'المعاينة غير متاحة'],
 'id': ['Semua', 'Baru', 'Baru', '%d efek', 'Sebelum', 'Sesudah', 'Lihat di linimasa', 'Menyiapkan pratinjau…', 'Pratinjau tidak tersedia'],
}
swift = root / 'engine/platform/ios/app/AureaStrings.swift'
source = swift.read_text(encoding='utf-8')
for lang, values in translations.items():
    catalog = dict(zip(('fx_browser_' + key for key in keys), values))
    folder = 'values' if lang == 'pt' else 'values-' + lang
    xml = root / 'android/app/src/main/res' / folder / 'strings.xml'
    text = xml.read_text(encoding='utf-8')
    assert 'name="fx_browser_all"' not in text
    rows = ''.join(f'    <string name="{key}">{escape(value.replace("%d", "%1$d"))}</string>\n' for key, value in catalog.items())
    xml.write_text(text.replace('</resources>', rows + '</resources>'), encoding='utf-8')
    marker = f'private static let {lang}: [String: String] = ['
    rows = ''.join('\n        ' + json.dumps(key) + ': ' + json.dumps(value, ensure_ascii=False) + ',' for key, value in catalog.items())
    assert marker in source
    source = source.replace(marker, marker + rows, 1)
swift.write_text(source, encoding='utf-8')
