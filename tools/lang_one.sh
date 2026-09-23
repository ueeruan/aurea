#!/usr/bin/env bash
# Um idioma so, com espera maior: isola o caso do arabe sem rodar o ciclo todo.
set -u
ADB="C:/Users/SnyX/AppData/Local/Android/sdk/platform-tools/adb.exe"
PKG="com.aurea.aurea.debug"
TAG="${1:-ar}"
WAIT="${2:-18}"
mkdir -p docs/idiomas
$ADB shell run-as $PKG sh -c "mkdir -p shared_prefs && printf '%s' '<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\" ?><map><string name=\"idioma\">$TAG</string></map>' > shared_prefs/aurea.settings.xml && cat shared_prefs/aurea.settings.xml"
$ADB shell am force-stop $PKG
$ADB shell am start -n $PKG/com.aurea.aurea.MainActivity >/dev/null 2>&1
sleep "$WAIT"
$ADB exec-out screencap -p > "docs/idiomas/one_$TAG.png" 2>/dev/null
echo "$TAG -> docs/idiomas/one_$TAG.png ($(wc -c < "docs/idiomas/one_$TAG.png") bytes)"
