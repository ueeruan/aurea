# Acrescenta textos nos 7 idiomas (Android strings.xml e iOS AureaStrings.swift).
# Uso: python add_strings_024.py arquivo.json  ->  {"chave": {"pt": "...", "en": ...}}
# No Android "%1$s"; no iOS vira "%1$@". Chave existente é substituída.
import json, re, sys
from xml.sax.saxutils import escape

ROOT = 'D:/Users/Ruan/Documents/Aureabeta-vivo/'
RES = ROOT + 'android/app/src/main/res/'
SWIFT = ROOT + 'engine/platform/ios/app/AureaStrings.swift'
FOLDERS = {'pt': 'values', 'en': 'values-en', 'es': 'values-es', 'ru': 'values-ru',
           'hi': 'values-hi', 'ar': 'values-ar', 'id': 'values-id'}

def android_text(t):
    t = escape(t).replace("'", "\\'").replace('"', '\\"')
    return t

def add_android(lang, entries):
    path = RES + FOLDERS[lang] + '/strings.xml'
    s = open(path, encoding='utf-8').read()
    for key, text in entries:
        line = '    <string name="%s">%s</string>\n' % (key, android_text(text))
        pat = re.compile(r'^\s*<string name="%s"[^>]*>.*?</string>\n' % re.escape(key), re.M | re.S)
        if pat.search(s):
            s = pat.sub(lambda m: line, s, count=1)
        else:
            i = s.rindex('</resources>')
            s = s[:i] + line + s[i:]
    open(path, 'w', encoding='utf-8').write(s)

def swift_text(t):
    t = re.sub(r'%(\d+)\$s', r'%\1$@', t).replace('%s', '%@')
    return t.replace('\\', '\\\\').replace('"', '\\"')

def add_swift(table):
    s = open(SWIFT, encoding='utf-8').read()
    for lang in FOLDERS:
        start = s.index('    private static let %s: [String: String] = [' % lang)
        end = s.index('\n    ]', start)
        block = s[start:end]
        for key, text in table[lang]:
            line = '        "%s": "%s",' % (key, swift_text(text))
            pat = re.compile(r'^        "%s": ".*?",$' % re.escape(key), re.M)
            if pat.search(block):
                block = pat.sub(lambda m: line, block, count=1)
            else:
                block = block + '\n' + line
        s = s[:start] + block + s[end:]
    open(SWIFT, 'w', encoding='utf-8').write(s)

data = json.load(open(sys.argv[1], encoding='utf-8'))
table = {lang: [] for lang in FOLDERS}
for key, langs in data.items():
    missing = [l for l in FOLDERS if l not in langs]
    if missing: raise SystemExit('faltam idiomas em %s: %s' % (key, missing))
    for l in FOLDERS: table[l].append((key, langs[l]))
for l in FOLDERS: add_android(l, table[l])
add_swift(table)
print('ok', len(data), 'chave(s)')
