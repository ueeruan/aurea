#!/usr/bin/env bash
# Ciclo de idiomas no emulador (Fase 8.1).
#
# Escreve a preferencia do app direto (o APK e debugavel), forca a parada e
# abre de novo: e o mesmo caminho do usuario — a escolha mora na preferencia e
# entra no `attachBaseContext` na criacao da Activity.
set -u
ADB="C:/Users/SnyX/AppData/Local/Android/sdk/platform-tools/adb.exe"
PKG="com.aurea.aurea.debug"
OUT="docs/idiomas"

mkdir -p "$OUT"

write_pref() {
  local tag="$1"
  if [ "$tag" = "system" ]; then
    $ADB shell run-as $PKG sh -c "rm -f shared_prefs/aurea.settings.xml" >/dev/null 2>&1
    return
  fi
  $ADB shell run-as $PKG sh -c "mkdir -p shared_prefs && printf '%s' '<?xml version=\"1.0\" encoding=\"utf-8\" standalone=\"yes\" ?><map><string name=\"idioma\">$tag</string></map>' > shared_prefs/aurea.settings.xml" >/dev/null 2>&1
}

for tag in pt-BR ar ru hi en es id system; do
  write_pref "$tag"
  $ADB shell am force-stop $PKG
  $ADB shell am start -n $PKG/com.aurea.aurea.MainActivity >/dev/null 2>&1
  sleep 7
  $ADB exec-out screencap -p > "$OUT/home_$tag.png" 2>/dev/null
  echo "$tag -> $OUT/home_$tag.png ($(wc -c < "$OUT/home_$tag.png") bytes)"
done
